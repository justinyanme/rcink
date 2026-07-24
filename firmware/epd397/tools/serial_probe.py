#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pyserial"]
# ///
"""Non-interactive serial probe for the epd397 dev console.

Opens the USB-CDC port with DTR/RTS deasserted (the README's tested monitor
settings — avoids toggling the S3 into download mode), optionally injects a
sequence of console keys with per-key delays, and streams output for a fixed
window. Lets an agent exercise the device console hands-free.

    uv run tools/serial_probe.py --port /dev/cu.usbmodemNNNNN --seconds 8
    uv run tools/serial_probe.py --port /dev/cu.usbmodemNNNNN --send "sq" --seconds 6
"""

from __future__ import annotations

import argparse
import sys
import time

import serial


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument(
        "--send",
        default="",
        help="console keys to inject, comma-separated for explicit steps (e.g. 'n,n,b'); "
        "a bare string sends each char in turn",
    )
    ap.add_argument("--key-delay", type=float, default=1.5, help="seconds between injected keys")
    ap.add_argument("--start-delay", type=float, default=1.0, help="seconds to wait before first key")
    args = ap.parse_args()

    keys = [k for k in args.send.split(",")] if "," in args.send else list(args.send)

    s = serial.Serial()
    s.port = args.port
    s.baudrate = args.baud
    s.dtr = False
    s.rts = False
    s.timeout = 0.1
    s.open()
    s.dtr = False
    s.rts = False

    t0 = time.time()
    next_key_at = t0 + args.start_delay
    ki = 0
    try:
        while time.time() - t0 < args.seconds:
            if ki < len(keys) and time.time() >= next_key_at:
                k = keys[ki]
                sys.stdout.write(f"\n>>> send {k!r}\n")
                sys.stdout.flush()
                s.write(k.encode())
                s.flush()
                ki += 1
                next_key_at = time.time() + args.key_delay
            data = s.read(4096)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.flush()
    finally:
        s.close()


if __name__ == "__main__":
    main()
