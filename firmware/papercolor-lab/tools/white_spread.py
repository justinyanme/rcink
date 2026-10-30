#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow>=11"]
# ///
"""Colour-history ghost under white: draw image `bands` with own_color (eight bands of different
inks), then white over everything, and report the max / mean pairwise camera ΔE76 between the
eight band centres (0 = no trace of the previous colours; native white measured 2.2 / 1.2).

Usage: white_spread.py OUT [native|own] [--port P] [--camera C]
"""

import argparse
import itertools
import math
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageStat

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lutcodec as lc
import own_color as oc
from own_grid import at, lab


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("white", choices=["native", "own"])
    ap.add_argument("--port", default="/dev/cu.usbmodem83301")
    ap.add_argument("--camera", default="android:3B161600Z2U00000")
    a = ap.parse_args()
    steps = [*lc.lut_steps(oc.lut()), f"show bands custom {oc.KNOBS} map={oc.MAP}"]
    steps.append("show white native" if a.white == "native" else f"show white custom {oc.KNOBS} map={oc.MAP}")
    lab_py = Path(__file__).resolve().parents[1] / "lab.py"
    cmd = ["uv", "run", str(lab_py), "trial", a.out, "--port", a.port, "--camera", a.camera]
    subprocess.run([*cmd, "--steps", ",".join([*steps, "photo"]), "--no-restore"], check=True)
    im = Image.open(sorted(Path(a.out).glob("*.jpg"))[-1]).convert("RGB")
    w, h = im.size
    labs = []
    for band in range(8):
        x, y = at(0.5, (37 + 75 * band) / 600)
        box = (int(x * w) - 60, int(y * h) - 12, int(x * w) + 60, int(y * h) + 12)
        labs.append(lab(ImageStat.Stat(im.crop(box)).mean))
    d = [math.dist(p, q) for p, q in itertools.combinations(labs, 2)]
    print("bands (ink K W Y R - B G -) L*/a*/b*:", " ".join(f"{p[0]:.0f}/{p[1]:.0f}/{p[2]:.0f}" for p in labs))
    print(f"spread max dE {max(d):.1f} mean {sum(d) / len(d):.1f}")


if __name__ == "__main__":
    main()
