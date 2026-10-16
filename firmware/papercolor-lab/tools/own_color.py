#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# ///
"""Own six-colour waveform: ONE full-screen 4 bpp custom pass (PSR 1F A9) over the current image.

Ghost rule (2026-10-16): a pixel that idles while others drive shows the previous image,
so every used LUT row is active for the whole pass (368 frames, no idle padding). Each row
starts with a clean-mono cycle (black 85 + white tail) that erases history, then its colour
train: N x (yellow t0 code 1, black t0 code 2) ends blue when the last pulse is black
(>= 4 frames), green when it is 1 frame, yellow when it ends on yellow; N x (black,
code 9) ending on code 9 gives red. Slow white/black pads fill rows to equal length.
README "Own six-colour waveform".

Usage: own_color.py OUT IMAGE... [--port P] [--camera C]  -> one lab.py Trial, photo after each.
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lutcodec as lc  # noqa: E402

KNOBS = "setup=tickey psr=1FA9 booster=none win=1 pack=4"
TAIL = [(8, 60), (1, 5), (8, 20), (2, 1), (8, 5)]
CLEAN = [(2, 85), *TAIL]  # 176 frames: erases history like own_mono
FRAMES = 368

# 2-frame black / t1 code 2 AC (structure from TICKEY quality phase 1) erases colour history
# under white; the tail ends without the yellow flick: neutral white.
ERASE = [(20, (2, 2), (2, 2, 1))]
WHITE = [(8, 20), *ERASE, (2, 60), *TAIL, (2, 30), (8, 60), (2, 2), (8, 25)]
BLACK = [(2, 16), *TAIL, (2, 85), *TAIL, (2, 85)]
YELLOW = [(8, 68), (2, 68), *CLEAN, (4, (1, 10), (9, 4))]
RED = [(8, 6), (2, 6), *CLEAN, (12, (2, 10), (9, 5))]
BLUE = [*CLEAN, (12, (1, 10), (2, 6))]
GREEN = [(8, 52), (2, 52), *CLEAN, (8, (1, 10), (2, 1))]

# LUT row - 1 per ink code (K W Y R - B G -); rows 5 and 8 unused.
ROWS = [WHITE, BLACK, YELLOW, RED, [], BLUE, GREEN, []]
MAP = "10231561"


def lut() -> bytes:
    if lengths := {lc.frames(r) for r in ROWS if r} - {FRAMES}:
        raise ValueError(f"every used row must last {FRAMES} frames, got {sorted(lengths)}")
    return lc.build(lc.right_align([None, *(r or None for r in ROWS)]))


def steps(images: list[str]) -> list[str]:
    out = lc.lut_steps(lut())
    for image in images:
        out += [f"show {image} custom {KNOBS} map={MAP}", "photo"]
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
