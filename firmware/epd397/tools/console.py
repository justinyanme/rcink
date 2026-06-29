#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# ///
"""Hands-free serial console for the epd397 USB-Serial/JTAG (HWCDC) port.

The ESP32-S3 USB-Serial/JTAG resets into ROM download mode when the host asserts
DTR/RTS — which pyserial does on open. This reader instead opens the call-out
device raw via os.open + termios (exactly like `cat`), never touching the modem
control lines, so the running app is never knocked into download. It can still
inject console keys (plain data writes don't toggle DTR/RTS).

    uv run tools/console.py --seconds 10
    uv run tools/console.py --send "t,m,s" --seconds 20
    uv run tools/console.py --send "p" --seconds 30 > /tmp/frame.txt   # frame dump
"""

from __future__ import annotations

import argparse
import fcntl
import os
import select
import struct
import sys
import termios
import time
import tty


def open_raw(port: str, baud: int, *, dtr: bool) -> int:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    # iflag, oflag, cflag, lflag, ispeed, ospeed, cc
    attrs[2] |= termios.CLOCAL | termios.CREAD  # ignore modem ctrl, enable rx
    attrs[2] &= ~termios.CRTSCTS  # no hw flow control
    attrs[2] &= ~termios.HUPCL  # don't drop DTR on close (no reset)
    speed = getattr(termios, f"B{baud}", termios.B115200)
    attrs[4] = speed
    attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    # HWCDC (USB-Serial/JTAG) gates TX on DTR (host "connected"). Set DTR steady
    # ON and RTS steady OFF: ROM download needs an RTS/EN *pulse* sequence, so a
    # static DTR can't trip it. Never toggle these again.
    fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack("I", termios.TIOCM_RTS))
    op = termios.TIOCMBIS if dtr else termios.TIOCMBIC
    fcntl.ioctl(fd, op, struct.pack("I", termios.TIOCM_DTR))
    return fd


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem83401")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--send", default="", help="keys to inject; comma-separated for explicit steps")
    ap.add_argument("--key-delay", type=float, default=1.5)
    ap.add_argument("--start-delay", type=float, default=1.0)
    ap.add_argument(
        "--dtr",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="assert DTR steady (HWCDC needs it to emit TX); default on",
    )
    args = ap.parse_args()

    keys = args.send.split(",") if "," in args.send else list(args.send)
    fd = open_raw(args.port, args.baud, dtr=args.dtr)
    t0 = time.time()
    next_key_at = t0 + args.start_delay
    ki = 0
    try:
        while time.time() - t0 < args.seconds:
            if ki < len(keys) and time.time() >= next_key_at:
                k = keys[ki]
                sys.stderr.write(f"\n>>> send {k!r}\n")
                sys.stderr.flush()
                os.write(fd, k.encode())
                ki += 1
                next_key_at = time.time() + args.key_delay
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                data = os.read(fd, 4096)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.flush()
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
