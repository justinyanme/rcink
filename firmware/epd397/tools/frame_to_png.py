#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow"]
# ///
"""Decode an epd397 console `p` frame dump (read from stdin) into a PNG.

Pairs with tools/console.py for the USB-Serial/JTAG board:

    uv run tools/console.py --send p --seconds 28 > /tmp/dump.txt
    uv run tools/frame_to_png.py < /tmp/dump.txt -o /tmp/frame.png
"""

from __future__ import annotations

import argparse
import re
import sys

from PIL import Image

BEGIN_RE = re.compile(r"\[frame\] begin w=(\d+) h=(\d+) bpp=2 bytes=(\d+)")
HEX_RE = re.compile(r"^[0-9A-Fa-f]+$")
GRAY_LEVEL = (0, 85, 170, 255)  # code 0=black .. 3=white, matches gray_pack.h


def decode(text: str) -> Image.Image:
    w = h = nbytes = 0
    started = False
    chunks: list[str] = []
    for line in text.splitlines():
        line = line.strip()
        if not started:
            if m := BEGIN_RE.search(line):
                w, h, nbytes = int(m.group(1)), int(m.group(2)), int(m.group(3))
                started = True
            continue
        if line == "[frame] end":
            break
        if HEX_RE.fullmatch(line):
            chunks.append(line)
    if not started:
        raise SystemExit("no [frame] begin marker found in input")
    data = bytes.fromhex("".join(chunks))
    if len(data) != nbytes:
        raise SystemExit(f"frame length mismatch: got {len(data)}, expected {nbytes}")

    img = Image.new("L", (w, h), 255)
    px = img.load()
    row_bytes = w // 4
    for y in range(h):
        base = y * row_bytes
        for x in range(w):
            code = (data[base + (x >> 2)] >> (6 - ((x & 3) << 1))) & 0x3
            px[x, y] = GRAY_LEVEL[code]
    return img.convert("RGB")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="/tmp/epd397_frame.png")
    args = ap.parse_args()
    img = decode(sys.stdin.read())
    img.save(args.out)
    print(args.out)


if __name__ == "__main__":
    main()
