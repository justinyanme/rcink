#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow>=11"]
# ///
"""Own staged colour on image `grid` (48-colour attempt, route C): 8 bases x 8 modifiers = 64 classes.

Two full-screen 4 bpp passes (PSR 1F A9): pass 1 (field 0:3) draws eight AC-train bases, pass 2
(field 3:3) adds a short partial pulse or train that shifts each base. Prints the largest set of
patches pairwise distinct at camera ΔE76 >= 5 / 8 (randomised greedy), raw and white-normalised
(each channel scaled so patch 0, the white base without modifier, equals the native white
164,175,176 measured in daylight: removes the room-light level, which compresses all ΔE).

Usage: own_grid.py OUT [--port P] [--camera C]
"""

import argparse
import json
import math
import random
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageStat

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lutcodec as lc

SHAKE = [(4, (8, 6), (2, 10))]
BASES = [  # W K R B Y G pink orange (search48g/h + base-6 trial, 2026-10-16)
    [*SHAKE, (8, 60), (1, 5), (8, 20), (2, 1), (8, 5)],
    [(8, (8, 6), (2, 10))],
    [(8, (2, 10), (9, 5))],
    [(12, (1, 10), (2, 6))],
    [*SHAKE, (4, (1, 10), (9, 4))],
    [(8, (1, 10), (2, 1))],
    [(8, (2, 10), (9, 5)), (8, 4)],  # pink: red + 4 white frames
    [(8, (2, 10), (9, 5)), (1, 3)],
]
MODIFIERS = [
    [],
    [(2, 1)],
    [(2, 1), (8, 1)],
    [(8, 6)],
    [(1, 1)],
    [(1, (2, 5), (9, 3))],  # half a red cycle
    [(1, (1, 5), (2, 1))],  # half a green cycle
    [(2, 3, 1)],  # t1 code 2 (weak yellow)
]
PASSES = [("0:3", BASES), ("3:3", MODIFIERS)]
NATIVE_WHITE = (164, 175, 176)
# panel corners as photo fractions (TL, TR, BR, BL) for the bench camera framing
CORNERS = [(0.192, 0.280), (0.646, 0.292), (0.606, 0.888), (0.215, 0.882)]


def lab(rgb: tuple[int, ...]) -> tuple[float, float, float]:
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


def at(u: float, v: float) -> tuple[float, float]:
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = CORNERS
    top = (x0 + (x1 - x0) * u, y0 + (y1 - y0) * u)
    bot = (x3 + (x2 - x3) * u, y3 + (y2 - y3) * u)
    return top[0] + (bot[0] - top[0]) * v, top[1] + (bot[1] - top[1]) * v


def luts() -> list[bytes]:
    return [lc.build(lc.right_align([None, *(r or None for r in rows)])) for _, rows in PASSES]


def steps() -> list[str]:
    out: list[str] = []
    for (field, _), table in zip(PASSES, luts(), strict=True):
        out += lc.lut_steps(table)
        out.append(f"show grid custom setup=tickey psr=1FA9 booster=none win=1 pack=4 field={field}")
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--port", default="/dev/cu.usbmodem83301")
    ap.add_argument("--camera", default="android:3B161600Z2U00000")
    a = ap.parse_args()
    lab_py = Path(__file__).resolve().parents[1] / "lab.py"
    cmd = ["uv", "run", str(lab_py), "trial", a.out, "--port", a.port, "--camera", a.camera]
    subprocess.run([*cmd, "--steps", ",".join([*steps(), "photo"]), "--no-restore"], check=True)
    im = Image.open(sorted(Path(a.out).glob("*.jpg"))[-1]).convert("RGB")
    w, h = im.size
    cols = []
    for v in range(64):
        x, y = at((25 + 50 * (v % 8)) / 400, (37 + 75 * (v // 8)) / 600)
        box = (int(x * w) - 18, int(y * h) - 20, int(x * w) + 18, int(y * h) + 20)
        cols.append(tuple(round(c) for c in ImageStat.Stat(im.crop(box)).mean))
    (Path(a.out) / "patches.json").write_text(json.dumps(cols))
    white = cols[0]
    normed = [tuple(min(255.0, c[i] * NATIVE_WHITE[i] / white[i]) for i in range(3)) for c in cols]
    for name, rgb in (("raw", cols), ("white-normalised", normed)):
        labs = [lab(c) for c in rgb]
        print(name, " ".join(f"dE>={th}: {largest_distinct(labs, th)}" for th in (5, 8)))


def largest_distinct(labs: list[tuple[float, float, float]], th: float, tries: int = 3000) -> int:
    rng = random.Random(0)
    best = 0
    for _ in range(tries):
        kept: list[tuple[float, float, float]] = []
        for i in rng.sample(range(len(labs)), len(labs)):
            if all(math.dist(labs[i], k) >= th for k in kept):
                kept.append(labs[i])
        best = max(best, len(kept))
    return best


if __name__ == "__main__":
    main()
