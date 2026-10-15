#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# ///
"""Own six-colour waveform: two 2 bpp custom passes over the current image (route C).

Pass 1 drives white / blue / yellow and black (red and green go black too); pass 2
drives green / red and black again, white / blue / yellow idle. Idle pixels drift
while others drive, so black is re-driven and red sits in pass 2. Colours are AC
trains: N x (yellow t0 code 1, black t0 code 2) ends blue when the last pulse is
black (>=4 frames), green when it is 1 frame, yellow when it ends on yellow;
N x (black, code 9) ending on code 9 gives red. Measured 2026-10-16 (README
"Own six-colour waveform").

Usage: own_color.py OUT IMAGE [--port P] [--camera C]  -> one lab.py Trial, photo after.
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lutcodec as lc  # noqa: E402

KNOBS = "setup=tickey psr=0FA9 booster=none win=1 pack=2"
SHAKE = [(4, (8, 6), (2, 10))]  # white/black shaking: erases colour and mono history
TO_WHITE = SHAKE + [(8, 60), (1, 5), (8, 20), (2, 1), (8, 5)]
TO_BLACK = [(8, (8, 6), (2, 10))]  # shaking erases colour history; plain 2x85 keeps it
BLUE = [(12, (1, 10), (2, 6))]
RED = [(8, (2, 10), (9, 5))]
YELLOW = SHAKE + [(4, (1, 10), (9, 4))]
GREEN = [(8, (1, 10), (2, 1))]

# map digit i = LUT row - 1 for ink i (K W Y R - B G -).
PASS1 = ("30233133", [TO_WHITE, BLUE, YELLOW, TO_BLACK])
PASS2 = ("30020010", [[], GREEN, RED, TO_BLACK])  # black re-driven: idle black drifts maroon


def lut(rows: list[list]) -> bytes:
    return lc.build(lc.right_align([None, *(r or None for r in rows), None, None, None, None]))


def steps(image: str) -> list[str]:
    out: list[str] = []
    for m, rows in (PASS1, PASS2):
        out += lc.lut_steps(lut(rows)) + [f"show {image} custom {KNOBS} map={m}"]
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("image")
    ap.add_argument("--port", default="/dev/cu.usbmodem83301")
    ap.add_argument("--camera", default="android:3B161600Z2U00000")
    a = ap.parse_args()
    for name, (_, rows) in (("pass1", PASS1), ("pass2", PASS2)):
        n = max(lc.frames(r) for r in rows)
        print(f"{name}: {n} frames ~{n * 11.75 / 1000:.2f} s")
    lab = Path(__file__).resolve().parents[1] / "lab.py"
    cmd = [
        "uv",
        "run",
        str(lab),
        "trial",
        a.out,
        "--port",
        a.port,
        "--camera",
        a.camera,
        "--steps",
        ",".join([*steps(a.image), "photo"]),
        "--no-restore",
    ]
    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
