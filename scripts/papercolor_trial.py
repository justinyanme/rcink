#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["typer>=0.12", "pyserial>=3.5"]
# ///
"""Bounded waveform comparison: one USB session, settled P4 JPEG per trial."""

import json
import subprocess
import time
from pathlib import Path
from typing import Annotated

import serial
import typer

ROOT = Path(__file__).resolve().parents[1]


def main(
    port: Annotated[str, typer.Option()],
    out: Annotated[Path, typer.Option(help="New evidence directory")],
    modes: Annotated[str, typer.Option(help="Explicit finite sequence: N/F/Q/M/C/I, d=diagnostics")] = "N",
) -> None:
    if not modes or len(modes) > 5 or any(mode not in "NFQMCId" for mode in modes):
        raise typer.BadParameter("Choose one to five modes from N/F/Q/M/C/I/d")
    if not Path(port).exists():
        raise typer.BadParameter(f"Port absent: {port}")
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)

    def photo(name: str) -> None:
        subprocess.run(
            [
                "uv",
                "run",
                "--project",
                str(ROOT.parent / "rceye"),
                "eye",
                "--eye",
                "p4",
                "photo",
                "--out",
                str(out / f"{name}.jpg"),
            ],
            check=True,
            timeout=30,
        )

    # Preflight camera before issuing any waveform command.
    photo("before")
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1, exclusive=True)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    with (out / "serial.log").open("wb") as log, (out / "events.jsonl").open("w") as events:

        def wait_for(marker: bytes, seconds: float) -> bytes:
            received = bytearray()
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                data = connection.read(4096)
                if not data:
                    if not Path(port).exists():
                        raise RuntimeError("PaperColor disconnected; no reconnect")
                    continue
                log.write(data)
                log.flush()
                print(data.decode(errors="replace"), end="", flush=True)
                received.extend(data)
                if any(fatal in received for fatal in (b"Guru Meditation", b"[power] fatal:", b"[boot] fatal:")):
                    raise RuntimeError("Firmware fatal error")
                # Require the complete acknowledgement, including uptime;
                # a truncated USB response must not pass this hardware check.
                if (start := received.find(marker)) >= 0 and b"\n" in received[start + len(marker) :]:
                    return bytes(received)
            raise TimeoutError(f"No {marker!r} within {seconds} seconds")

        def command(char: str, seconds: float) -> bytes:
            events.write(
                json.dumps({"t_ns": time.monotonic_ns(), "clock": "host-monotonic", "label": f"console {char!r}"})
                + "\n"
            )
            events.flush()
            if connection.write(char.encode("ascii")) != 1:
                raise RuntimeError("Short console write")
            connection.flush()
            return wait_for(f"[console] done {char} uptime_ms=".encode(), seconds)

        dirty = False
        try:
            connection.open()
            # Opening native USB can reset this unit; do not send into boot.
            wait_for(b"[console] ready", 60)
            command("p", 5)
            for index, mode in enumerate(modes):
                dirty = True
                result = command(mode, 150)
                photo(f"{index:02d}-{mode}")
                expected = b"[render] draw=" if mode == "d" else b"[trial] complete"
                if b"[trial] FAILED" in result or expected not in result:
                    raise RuntimeError(f"Trial {mode} failed; see serial.log")
                # Every experiment is followed by a complete native waveform.
                command("r", 60)
                dirty = False
                photo(f"{index:02d}-restored")
            command("p", 5)
        finally:
            try:
                if dirty and connection.is_open:
                    # Covers camera failure too. Never retry a waveform or
                    # reopen USB; request one native dashboard restoration.
                    command("r", 60)
                    photo("recovered-after-failure")
            finally:
                connection.close()
    print(f"Evidence: {out}")


if __name__ == "__main__":
    app = typer.Typer(pretty_exceptions_show_locals=False)
    app.command()(main)
    app()
