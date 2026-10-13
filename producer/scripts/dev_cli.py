"""PaperColor USB console: explicit port, one exclusive connection, no reconnect.

    uv run dev-cli session s --port /dev/cu.usbmodem20111401 --timeout 5 --save state.log
    uv run dev-cli session r --port /dev/cu.usbmodem20111401 --timeout 40
    uv run dev-cli log --port /dev/cu.usbmodem20111401 --timeout 10

`send` is an alias for `session`: command responses are collected on the same
connection. DTR/RTS remain inactive; opening the console must not request reset.
The firmware exposes the console while externally powered. A missing/disconnected port fails;
the CLI never switches ports or retries a command.
"""

from __future__ import annotations

import codecs
import json
import sys
import time
from contextlib import ExitStack, contextmanager
from pathlib import Path
from typing import Annotated

import serial
import typer

app = typer.Typer(add_completion=False, help=__doc__, no_args_is_help=True)
Port = Annotated[str, typer.Option("--port", "-p", help="Exact target port; required, never autodiscovered.")]
Timeout = Annotated[float, typer.Option(min=0, help="Session seconds, including sending; 0 = until Ctrl-C.")]
Save = Annotated[Path | None, typer.Option(help="Save original received bytes to a new file.")]


@contextmanager
def open_port(port: str):
    """Configure control lines before opening; never let Serial's constructor open."""
    if not Path(port).exists():
        raise serial.SerialException(f"Port does not exist: {port}")
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1, exclusive=True)
    try:
        connection.dtr = False
        connection.rts = False
        connection.port = port
        connection.open()
        yield connection
    finally:
        connection.close()


def run_session(
    *,
    port: str,
    chars: str,
    delay: float,
    timeout: float,
    save: Path | None,
    once: bool,
    start_delay: float = 0,
    events: Path | None = None,
) -> None:
    try:
        payload = chars.encode("ascii")
    except UnicodeEncodeError as error:
        raise typer.BadParameter("Console commands must be ASCII.", param_hint="chars") from error
    try:
        with ExitStack() as stack:
            connection = stack.enter_context(open_port(port))
            output = stack.enter_context(save.open("xb")) if save else None
            event_log = stack.enter_context(events.open("x")) if events else None
            deadline = time.monotonic() + timeout if timeout else None
            next_send = time.monotonic() + start_delay
            index = 0
            decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
            marker_tail = b""
            while deadline is None or time.monotonic() < deadline:
                if not Path(port).exists():
                    raise serial.SerialException(f"Port disconnected: {port}")
                if index < len(payload) and time.monotonic() >= next_send:
                    byte = bytes([payload[index]])
                    if event_log:
                        timestamp = time.monotonic_ns()
                        event_log.write(
                            json.dumps(
                                {
                                    "t_ns": timestamp,
                                    "clock": "host-monotonic",
                                    "label": f"console {byte.decode('ascii')!r}",
                                    "host_monotonic_ns": timestamp,
                                    "event": "console-write-start",
                                    "command": byte.decode("ascii"),
                                },
                                ensure_ascii=False,
                            )
                            + "\n"
                        )
                        event_log.flush()
                    if connection.write(byte) != 1:
                        raise serial.SerialException(f"Short write sending command byte {index + 1}/{len(payload)}")
                    connection.flush()
                    index += 1
                    next_send = time.monotonic() + delay
                now = time.monotonic()
                waits = [0.1]
                if index < len(payload):
                    waits.append(max(0, next_send - now))
                if deadline is not None:
                    waits.append(max(0, deadline - now))
                connection.timeout = min(waits)
                data = connection.read(4096)
                if not data:
                    continue
                if output:
                    output.write(data)
                    output.flush()
                sys.stdout.write(decoder.decode(data))
                sys.stdout.flush()
                marker = marker_tail + data
                if any(failure in marker for failure in (b"[power] fatal:", b"[boot] fatal:", b"Guru Meditation")):
                    raise serial.SerialException("Firmware reported a fatal error; session stopped")
                if once and b"[sleep]" in marker:
                    break
                marker_tail = marker[-32:]
            if index < len(payload):
                raise serial.SerialException("Session ended before all commands were sent")
            sys.stdout.write(decoder.decode(b"", final=True))
            sys.stdout.flush()
    except (OSError, serial.SerialException) as error:
        typer.echo(f"Console failed on {port}: {error}", err=True)
        raise typer.Exit(code=1) from error


@app.command("session")
@app.command("send")
def cmd_session(
    port: Port,
    chars: Annotated[str, typer.Argument(help="Console chars; omit to collect logs only.")] = "",
    delay: Annotated[float, typer.Option(min=0, help="Inter-command delay in seconds.")] = 0.2,
    timeout: Timeout = 10.0,
    save: Save = None,
    start_delay: Annotated[float, typer.Option(min=0, help="Read boot logs before sending the first byte.")] = 0,
    events: Annotated[Path | None, typer.Option(help="New JSONL file: host monotonic command-write times.")] = None,
    once: Annotated[bool, typer.Option(help="Stop after the firmware '[sleep]' marker.")] = False,
) -> None:
    """Collect logs while sending commands; never reopen the port."""
    run_session(
        port=port,
        chars=chars,
        delay=delay,
        timeout=timeout,
        save=save,
        once=once,
        start_delay=start_delay,
        events=events,
    )


@app.command("log")
def cmd_log(
    port: Port,
    timeout: Timeout = 0.0,
    save: Save = None,
    once: Annotated[bool, typer.Option(help="Stop after the firmware '[sleep]' marker.")] = False,
) -> None:
    """Collect output until timeout/Ctrl-C; fail on disconnect."""
    run_session(port=port, chars="", delay=0, timeout=timeout, save=save, once=once)


def main() -> None:
    app()


if __name__ == "__main__":
    main()
