#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pyserial"]
# ///
"""Headless serial console for the Xteink X4 (ESP32-C3 USB-Serial/JTAG).

`pio device monitor` needs a TTY (fails in an agent shell), and toggling
DTR/RTS can latch the C3 USB-CDC into the ROM download stub. This opens the
port with DTR/RTS held inactive, optionally sends console command chars, then
reads for a fixed window — safe to run from a non-interactive shell.

    uv run tools/x4_console.py --read 12             # just listen
    uv run tools/x4_console.py --send f --read 8     # fetch, then listen
    uv run tools/x4_console.py --send P --capture /tmp/x4.pbm   # frame dump
"""

import argparse
import sys
import time

import serial


def open_port(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.dtr = False  # do not pulse the reset / boot straps on open
    ser.rts = False
    ser.timeout = 0.1
    ser.open()
    return ser


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem101")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--send", default="", help="console command chars to send first")
    ap.add_argument(
        "--delay", type=float, default=0.0, help="seconds to wait after open before sending (let the board boot)"
    )
    ap.add_argument("--read", type=float, default=10.0, help="seconds to read")
    args = ap.parse_args()

    ser = open_port(args.port, args.baud)
    if args.send:
        # Opening the port resets the C3; drain boot output, then send once the
        # console is ready so the command isn't lost during startup.
        end = time.monotonic() + args.delay
        while time.monotonic() < end:
            data = ser.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
        for ch in args.send:
            ser.write(ch.encode())
            ser.flush()
            time.sleep(0.05)

    deadline = time.monotonic() + args.read
    while time.monotonic() < deadline:
        data = ser.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
    ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
