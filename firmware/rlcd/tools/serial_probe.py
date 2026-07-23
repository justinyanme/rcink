#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# ///
"""Non-interactive serial probe for the rlcd USB-Serial/JTAG console.

Opens the call-out device directly instead of through pyserial, which resets
this S3 on open even when DTR/RTS are configured false. DTR is held steadily on
for HWCDC output and RTS steadily off; neither line is pulsed. The probe exits
cleanly when production deep sleep removes the USB device.

    uv run tools/serial_probe.py --port /dev/cu.usbmodemNNNNN --seconds 8
    uv run tools/serial_probe.py --port /dev/cu.usbmodemNNNNN --send "sq" --seconds 6
"""

from __future__ import annotations

import argparse
import errno
import fcntl
import os
import select
import struct
import sys
import termios
import time
import tty


def open_raw(port: str, baud: int) -> int:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[2] &= ~termios.CRTSCTS
    attrs[2] &= ~termios.HUPCL
    speed = getattr(termios, f"B{baud}", termios.B115200)
    attrs[4] = speed
    attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack("I", termios.TIOCM_RTS))
    fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack("I", termios.TIOCM_DTR))
    return fd


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--port",
        required=True,
        help="verified rlcd call-out device; port suffixes drift, so never reuse a stale value",
    )
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
    fd = open_raw(args.port, args.baud)

    t0 = time.time()
    next_key_at = t0 + args.start_delay
    ki = 0
    try:
        while time.time() - t0 < args.seconds:
            if ki < len(keys) and time.time() >= next_key_at:
                k = keys[ki]
                sys.stdout.write(f"\n>>> send {k!r}\n")
                sys.stdout.flush()
                os.write(fd, k.encode())
                ki += 1
                next_key_at = time.time() + args.key_delay
            readable, _, _ = select.select([fd], [], [], 0.1)
            if readable:
                data = os.read(fd, 4096)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.flush()
    except OSError as exc:
        if exc.errno not in {errno.EIO, errno.ENODEV, errno.ENXIO}:
            raise
        print(f"\n[probe] device disconnected: {exc}", file=sys.stderr)
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
