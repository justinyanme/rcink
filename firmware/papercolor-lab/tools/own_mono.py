#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# ///
"""Own ghost-free mono (route C): every pixel runs two full cycles to its target, history ignored.

white = tail, black 85, tail; black = black 85, tail, black 85 (tail = 8x60,1x5,8x20,2x1,8x5).
One 2 bpp `custom` pass with `map=10111111` (white -> LUT row 1, every other ink -> row 2).
Driving unchanged pixels too is what removes the ghost: the fast transition mono leaves them
idle (white-after-text ghost 2.10 vs 0.69-0.73 here, native 0.60; tools/ghost_metric.py on
the curated evidence photos, README "Own clean mono").

Usage: own_mono.py OUT IMAGE... [--port P] [--camera C]  -> one lab.py Trial, photo after each.
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lutcodec as lc  # noqa: E402

KNOBS = "setup=tickey psr=0FA9 booster=none win=1 pack=2 map=10111111"
TAIL = [(8, 60), (1, 5), (8, 20), (2, 1), (8, 5)]
BLACK = [(2, 85)]
WHITE_ROW = TAIL + BLACK + TAIL
BLACK_ROW = BLACK + TAIL + BLACK


def lut() -> bytes:
    return lc.build(lc.right_align([None, WHITE_ROW, BLACK_ROW, None, None, None, None, None, None]))


# Fast transition mono (README "Own mono waveform"): rows by code (old << 1) | new; only the
# two changing transitions drive, unchanged pixels idle (fast, but they keep the old ghost).
# Run as `custom` with KNOBS minus `map=`.
def fast_lut() -> bytes:
    return lc.build(lc.right_align([None, None, TAIL, BLACK, None, None, None, None, None]))


def steps(images: list[str]) -> list[str]:
    out = lc.lut_steps(lut())
    for image in images:
        out += [f"show {image} custom {KNOBS}", "photo"]
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("images", nargs="+")
    ap.add_argument("--port", default="/dev/cu.usbmodem83301")
    ap.add_argument("--camera", default="android:3B161600Z2U00000")
    a = ap.parse_args()
    lab = Path(__file__).resolve().parents[1] / "lab.py"
    cmd = ["uv", "run", str(lab), "trial", a.out, "--port", a.port, "--camera", a.camera]
    subprocess.run([*cmd, "--steps", ",".join(steps(a.images))], check=True)


if __name__ == "__main__":
    main()
