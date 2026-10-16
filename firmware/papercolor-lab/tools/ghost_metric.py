#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow>=11"]
# ///
"""Ghost metric for bench photos: mean |L - blur12(L)| over the panel's central area (high-pass
residue; text ghost raises it, flat field ~0.2-0.9) plus the area's CIELAB mean.

Crop = photo fractions x 0.26-0.56, y 0.38-0.78 (bench camera framing). Usage: ghost_metric.py PHOTO...
"""

import sys

from PIL import Image, ImageChops, ImageFilter, ImageStat

BOX = (0.26, 0.38, 0.56, 0.78)


def lab(rgb: list[float]) -> tuple[float, float, float]:
    def lin(c: float) -> float:
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    r, g, b = (lin(c) for c in rgb)
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.9505
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.089

    def f(t: float) -> float:
        return t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116

    return (116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z)))


for path in sys.argv[1:]:
    rgb = Image.open(path).convert("RGB")
    w, h = rgb.size
    box = (int(w * BOX[0]), int(h * BOX[1]), int(w * BOX[2]), int(h * BOX[3]))
    crop = rgb.crop(box)
    grey = crop.convert("L")
    hp = ImageStat.Stat(ImageChops.difference(grey, grey.filter(ImageFilter.GaussianBlur(12)))).mean[0]
    L, a, b = lab(ImageStat.Stat(crop).mean)
    print(f"hp={hp:.2f} Lab=({L:.0f},{a:.0f},{b:.0f})  {path.rsplit('/', 1)[-1]}")
