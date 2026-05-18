"""USB-CDC console helper for the rcpapercolor M5 PaperColor firmware.

Forwards keystrokes to `main.cpp::console_dispatch` (enabled by the
`RCPAPER_DEV_CONSOLE` build flag in platformio.ini).

    rcpaper-cli send f          # refetch + render
    rcpaper-cli send r          # re-render current page
    rcpaper-cli send n          # next page
    rcpaper-cli send mmm        # cycle EPD mode 3 times
    rcpaper-cli send s          # print state dump (read log via `rcpaper-cli log`)
    rcpaper-cli send h          # show on-device help

    rcpaper-cli log             # tail serial output until Ctrl-C
    rcpaper-cli log --once      # read until next deep-sleep marker, then exit

The device must be flashed with `-DRCPAPER_DEV_CONSOLE`; without it the
firmware deep-sleeps right after the first render and the port drops.
"""

from __future__ import annotations

import os
import sys
import time

import serial
import typer
from loguru import logger

DEFAULT_PORT = "/dev/cu.usbmodem3101"

app = typer.Typer(add_completion=False, help=__doc__, no_args_is_help=True)


@app.command("send")
def cmd_send(
    chars: str = typer.Argument(..., help="Char(s) to send. See main.cpp::console_dispatch."),
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p"),
    delay: float = typer.Option(0.2, "--delay", help="Inter-char delay (s)."),
) -> None:
    """Send one or more chars to the device's USB-CDC dev console."""
    with serial.Serial(port, 115200, timeout=0.5) as s:
        for c in chars:
            s.write(c.encode("ascii"))
            s.flush()
            time.sleep(delay)
    logger.info(f"sent {chars!r} to {port}")


@app.command("log")
def cmd_log(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p"),
    once: bool = typer.Option(False, "--once", help="Exit on first '[sleep]' or after --timeout."),
    timeout: float = typer.Option(0.0, "--timeout", help="Wall-clock seconds (0 = forever)."),
    save: str | None = typer.Option(None, "--save", help="Also write a copy to this file."),
) -> None:
    """Stream serial output. Reopens on USB-CDC drops (deep sleep)."""
    deadline = (time.time() + timeout) if timeout > 0 else None
    out = open(save, "w") if save else None
    try:
        while True:
            if deadline is not None and time.time() >= deadline:
                logger.info("timeout reached")
                return
            if not os.path.exists(port):
                time.sleep(0.3)
                continue
            try:
                s = serial.Serial(port, 115200, timeout=0.3)
            except (OSError, serial.SerialException) as e:
                logger.warning(f"open {port}: {e}")
                time.sleep(0.5)
                continue
            logger.info(f"opened {port}")
            while True:
                if deadline is not None and time.time() >= deadline:
                    return
                try:
                    data = s.read(4096)
                except OSError as e:
                    logger.info(f"{port} dropped: {e}")
                    break
                if not data:
                    continue
                text = data.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                if out:
                    out.write(text)
                    out.flush()
                if once and "[sleep]" in text:
                    logger.info("sleep marker seen; exiting")
                    return
            try:
                s.close()
            except Exception:
                pass
    finally:
        if out:
            out.close()


def main() -> None:
    logger.remove()
    logger.add(sys.stderr, format="{level: <7} {message}", level="INFO")
    app()


if __name__ == "__main__":
    main()
