#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow"]
# ///
"""Turn a serial framebuffer dump from the X4 into a PNG.

The firmware's `P` console command prints the 1-bit compose buffer as hex
between markers:

    [fb] BEGIN 800x480 1bpp
    ffffff...   (100 bytes/line = 200 hex chars, 480 lines)
    [fb] END

1bpp convention (matches SSD1677): bit=1 -> white, bit=0 -> black ink, MSB
first. Reads the dump from stdin (or a file), writes a black-on-white PNG.

    uv run tools/x4_console.py --send P --read 14 | uv run tools/fb_to_png.py /tmp/x4.png
"""

import sys
from pathlib import Path

from PIL import Image


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/x4_frame.png"
    text = Path(sys.argv[2]).read_text() if len(sys.argv) > 2 else sys.stdin.read()

    w = h = 0
    rows: list[str] = []
    capturing = False
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("[fb] BEGIN"):
            dims = line.split()[2]  # e.g. 800x480
            w, h = (int(v) for v in dims.split("x"))
            rows = []
            capturing = True
            continue
        if line.startswith("[fb] END"):
            capturing = False
            break
        if capturing and line and all(c in "0123456789abcdefABCDEF" for c in line):
            rows.append(line)

    if not w or not h:
        print("no [fb] BEGIN/END block found in input", file=sys.stderr)
        return 1
    if len(rows) != h:
        print(f"warning: expected {h} rows, got {len(rows)}", file=sys.stderr)

    img = Image.new("1", (w, h), 1)  # 1 = white
    px = img.load()
    stride = w // 8
    for y, hexrow in enumerate(rows):
        if y >= h:
            break
        data = bytes.fromhex(hexrow)
        for byte_i in range(min(stride, len(data))):
            b = data[byte_i]
            for bit in range(8):
                x = byte_i * 8 + bit
                if x >= w:
                    break
                px[x, y] = 1 if (b & (0x80 >> bit)) else 0  # bit=1 white, 0 black
    img.convert("L").save(out)
    print(f"wrote {out} ({w}x{h}, {len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
