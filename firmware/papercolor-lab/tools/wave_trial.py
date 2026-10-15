# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow>=11"]
# ///
"""Run one Trial with our own LUT waveforms (custom mode) and measure the panel centre.

  uv run firmware/papercolor-lab/tools/wave_trial.py OUT 'NAME:ROWS|NAME:ROWS' 'step,step,...'

ROWS = 'rN=ITEMS;...' for LUT row N (0 VCOM, N = pixel code + 1; mono: r1 dark->dark,
r2 dark->light, r3 light->dark, r4 light->light). ITEMS = comma list of 'CxF' (drive code C,
F frames; 'CpPxF' = phase-P code) or '(A,B)*R' repeated pairs. Missing rows idle; rows are
right-aligned (lutcodec.right_align). Flow steps: 'L NAME' uploads a LUT, 'C <image>' drives
it in custom mode, anything else is a console line (e.g. 'show white native', 'photo').
Prints the centre-crop colour (x 0.30-0.55, y 0.40-0.60 of the camera frame) of each photo.
"""

import json
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageStat

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lutcodec as lc  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
PORT = "/dev/cu.usbmodem83301"
CAMERA = "android:3B161600Z2U00000"
KNOBS = "setup=tickey psr=0FA9 booster=none win=1 pack=2"
BAND_KNOBS = "setup=tickey psr=5FA9 booster=none win=1 pack=4 map=01234567"


def item(text: str) -> lc.Step:
    code, n = text.split("x")
    phase = 0
    if "p" in code:
        code, p = code.split("p")
        phase = int(p)
    return (int(code), int(n), phase)


def items(text: str) -> list[lc.Item]:
    out: list[lc.Item] = []
    for m in re.finditer(r"\(([^)]*)\)\*(\d+)|([^,()]+)", text):
        if m[3]:
            out.append(item(m[3]))
        else:
            a, b = m[1].split(",")
            out.append((int(m[2]), item(a), item(b)))
    return out


def lut(spec: str) -> bytes:
    rows: list[list[lc.Item] | None] = [None] * lc.ROWS
    for part in spec.split(";"):
        key, value = part.split("=")
        rows[int(key[1:])] = items(value)
    return lc.build(lc.right_align(rows))


def centre(path: Path) -> str:
    im = Image.open(path).convert("RGB")
    w, h = im.size
    r, g, b = (
        round(v) for v in ImageStat.Stat(im.crop((int(w * 0.30), int(h * 0.40), int(w * 0.55), int(h * 0.60)))).mean
    )
    return f"rgb=({r:3},{g:3},{b:3}) L~{round(0.299 * r + 0.587 * g + 0.114 * b):3}"


def main(out: str, specs: str, flow: str) -> None:
    luts = {name: lut(spec) for name, spec in (entry.split(":", 1) for entry in specs.split("|"))}
    steps = lc.lut_steps(next(iter(luts.values())))
    for s in flow.split(","):
        if s.startswith("L "):
            steps += lc.lut_steps(luts[s[2:]])
        elif s.startswith("C "):
            steps.append(f"show {s[2:]} custom {KNOBS}")
        elif s.startswith("B "):  # 4 bpp raw frame: value i -> LUT row i+1 (e.g. 'B bands')
            steps.append(f"show {s[2:]} custom {BAND_KNOBS}")
        else:
            steps.append(s)
    proc = subprocess.run(
        [
            "uv",
            "run",
            "firmware/papercolor-lab/lab.py",
            "trial",
            out,
            "--port",
            PORT,
            "--camera",
            CAMERA,
            "--steps",
            ",".join(steps),
        ],
        capture_output=True,
        text=True,
        cwd=ROOT,
    )
    last = ""
    for line in (ROOT / out / "events.jsonl").read_text().splitlines():
        e = json.loads(line)
        if e["event"] == "result" and e["fields"].get("mode") in ("custom", "native"):
            last = e["fields"].get("image", "cleanup")
        elif e["event"] == "photo":
            print(f"  {e['file'][:2]} after {last:8s} {centre(ROOT / out / e['file'])}")
        elif e["event"] == "error":
            print("ERROR", e["message"])
    if proc.returncode:
        raise SystemExit(f"trial failed: {proc.stderr[-800:]}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
