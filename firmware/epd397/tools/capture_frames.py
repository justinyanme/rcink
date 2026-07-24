#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow", "pyserial", "typer"]
# ///
"""Capture exact epd397 screenshots from the firmware framebuffer dump.

Requires firmware with the console `p` command. That command prints the last
packed 800x480 4-gray (2bpp) frame; this script decodes that native buffer into
PNG files, so screenshots match the real firmware renderer and font stack
(antialiased gray edges included).

    uv run tools/capture_frames.py --port /dev/cu.usbmodemNNNNN --open
"""

from __future__ import annotations

import datetime as dt
import re
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Annotated

import serial
import typer
from PIL import Image, ImageDraw, ImageFont

SCREEN_W = 800
SCREEN_H = 480
FRAME_BYTES = (SCREEN_W * SCREEN_H) // 4  # 96000, 2bpp

# 4-gray code -> displayed gray (0=black .. 3=white), matching gray_pack.h.
GRAY_LEVEL = (0, 85, 170, 255)

BEGIN_RE = re.compile(
    r"^\[frame\] begin w=(?P<w>\d+) h=(?P<h>\d+) bpp=2 bytes=(?P<bytes>\d+) "
    r"mode=(?P<mode>\S+) idx=(?P<idx>\d+) count=(?P<count>\d+) id=(?P<id>\S+)$"
)
STATE_RE = re.compile(r"\[state\] mode=(?P<mode>\S+) pages=(?P<pages>\d+) idx=(?P<idx>\d+)")
HEX_RE = re.compile(r"^[0-9A-Fa-f]+$")

app = typer.Typer(add_completion=False, help=__doc__)


@dataclass(slots=True)
class FrameDump:
    image: Image.Image
    mode: str
    page_id: str
    idx: int
    count: int


@dataclass(slots=True)
class DeviceState:
    mode: str
    pages: int
    idx: int


def unpack_gray_frame(data: bytes, *, width: int = SCREEN_W, height: int = SCREEN_H) -> Image.Image:
    expected = (width * height) // 4
    if len(data) != expected:
        raise ValueError(f"bad frame length: got {len(data)}, expected {expected}")
    if width % 4 != 0:
        raise ValueError(f"unsupported geometry: {width}x{height}")

    img = Image.new("L", (width, height), 255)
    px = img.load()
    for y in range(height):
        row = y * (width // 4)
        for x in range(width):
            byte = data[row + (x >> 2)]
            code = (byte >> (6 - ((x & 3) << 1))) & 0x3
            px[x, y] = GRAY_LEVEL[code]
    return img.convert("RGB")


def _line(ser: serial.Serial, *, timeout_s: float) -> str | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = ser.readline()
        if raw:
            return raw.decode("utf-8", errors="replace").strip()
    return None


def _drain(ser: serial.Serial, *, seconds: float = 0.5) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        ser.read(4096)


def _send(ser: serial.Serial, key: str) -> None:
    ser.write(key.encode("ascii"))
    ser.flush()


def ensure_console_ready(ser: serial.Serial, *, timeout_s: float = 45.0) -> None:
    _drain(ser, seconds=0.2)
    _send(ser, "s")
    deadline = time.monotonic() + timeout_s
    seen: list[str] = []
    while time.monotonic() < deadline:
        line = _line(ser, timeout_s=0.5)
        if not line:
            continue
        seen.append(line)
        if "[console] ready" in line or STATE_RE.search(line):
            return
    tail = "\n".join(seen[-12:])
    raise RuntimeError(f"console did not become ready; tail:\n{tail}")


def read_state(ser: serial.Serial, *, timeout_s: float = 4.0) -> DeviceState:
    _drain(ser, seconds=0.2)
    _send(ser, "s")
    deadline = time.monotonic() + timeout_s
    seen: list[str] = []
    while time.monotonic() < deadline:
        line = _line(ser, timeout_s=0.2)
        if not line:
            continue
        seen.append(line)
        if m := STATE_RE.search(line):
            return DeviceState(mode=m.group("mode"), pages=int(m.group("pages")), idx=int(m.group("idx")))
    tail = "\n".join(seen[-8:])
    raise RuntimeError(f"state response not seen; tail:\n{tail}")


def wait_for_refresh(ser: serial.Serial, *, timeout_s: float) -> int:
    _send(ser, "f")
    deadline = time.monotonic() + timeout_s
    pages = 0
    seen: list[str] = []
    while time.monotonic() < deadline:
        line = _line(ser, timeout_s=0.5)
        if not line:
            continue
        seen.append(line)
        if m := re.search(r"\[main\] fetched (\d+) pages", line):
            pages = int(m.group(1))
        if "[auto] next network refresh" in line:
            if pages <= 0:
                raise RuntimeError("refresh completed but fetched zero pages")
            return pages
        if "[main] fetch failed" in line:
            raise RuntimeError("firmware reported fetch failure")
    tail = "\n".join(seen[-12:])
    raise RuntimeError(f"refresh timed out; tail:\n{tail}")


def capture_frame(ser: serial.Serial, *, timeout_s: float = 25.0) -> FrameDump:
    _drain(ser, seconds=0.2)
    _send(ser, "p")
    deadline = time.monotonic() + timeout_s
    meta: re.Match[str] | None = None
    chunks: list[str] = []
    seen: list[str] = []
    while time.monotonic() < deadline:
        line = _line(ser, timeout_s=0.2)
        if not line:
            continue
        seen.append(line)
        if meta is None:
            meta = BEGIN_RE.match(line)
            continue
        if line == "[frame] end":
            data = bytes.fromhex("".join(chunks))
            w, h, n = int(meta.group("w")), int(meta.group("h")), int(meta.group("bytes"))
            if len(data) != n:
                raise RuntimeError(f"frame length mismatch: got {len(data)}, expected {n}")
            return FrameDump(
                image=unpack_gray_frame(data, width=w, height=h),
                mode=meta.group("mode"),
                page_id=meta.group("id"),
                idx=int(meta.group("idx")),
                count=int(meta.group("count")),
            )
        if HEX_RE.fullmatch(line):
            chunks.append(line)
    tail = "\n".join(seen[-12:])
    raise RuntimeError(f"frame dump timed out; tail:\n{tail}")


def _safe_name(s: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "-", s).strip("-") or "screen"


def _save_frame(frame: FrameDump, out_dir: Path, *, prefix: str) -> Path:
    native = out_dir / "native"
    native.mkdir(parents=True, exist_ok=True)
    name = f"{prefix}-{_safe_name(frame.page_id)}.png"
    path = native / name
    frame.image.save(path)
    return path


def _font(px: int):
    for path in ("/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc"):
        try:
            return ImageFont.truetype(path, px)
        except OSError:
            continue
    return ImageFont.load_default()


def _contact_sheet(paths: list[Path], out: Path) -> Path:
    cols = 2
    cell_w, cell_h = 820, 520
    rows = (len(paths) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell_w, 54 + rows * cell_h), (32, 32, 32))
    draw = ImageDraw.Draw(sheet)
    draw.text(
        (20, 16),
        f"epd397 exact framebuffer capture  {dt.datetime.now().astimezone().isoformat()}",
        fill="white",
        font=_font(18),
    )
    label_font = _font(12)
    for i, path in enumerate(paths):
        x = (i % cols) * cell_w + 20
        y = 54 + (i // cols) * cell_h
        draw.text((x, y), path.stem, fill=(220, 220, 220), font=label_font)
        sheet.paste(Image.open(path), (x, y + 22))  # native 800x480 fits the cell
    out.mkdir(parents=True, exist_ok=True)
    path = out / "contact-sheet.png"
    sheet.save(path)
    return path


@app.command()
def main(
    port: Annotated[str, typer.Option("--port", help="Identity-verified USB-CDC serial port.")],
    baud: Annotated[int, typer.Option("--baud")] = 115200,
    out: Annotated[
        Path | None, typer.Option("--out", help="Output directory. Default: /tmp/epd397-capture-<timestamp>.")
    ] = None,
    refresh: Annotated[
        bool, typer.Option("--refresh/--no-refresh", help="Fetch fresh Axiom pages before capture.")
    ] = True,
    open_contact: Annotated[bool, typer.Option("--open", help="Open contact-sheet.png after capture.")] = False,
) -> None:
    out = out or Path("/tmp") / ("epd397-capture-" + dt.datetime.now().strftime("%Y%m%d-%H%M%S"))
    out.mkdir(parents=True, exist_ok=True)

    saved: list[Path] = []
    with serial.Serial(port=port, baudrate=baud, timeout=0.1, dsrdtr=False, rtscts=False) as ser:
        ser.dtr = False
        ser.rts = False
        ensure_console_ready(ser)

        if refresh:
            pages = wait_for_refresh(ser, timeout_s=60.0)
            typer.echo(f"refreshed {pages} pages")
            _drain(ser, seconds=0.5)

        state = read_state(ser)
        if state.mode == "deck":
            _send(ser, "m")
            time.sleep(4.0)  # 4-gray refresh ~3s

        _send(ser, "t")
        time.sleep(4.0)
        saved.append(_save_frame(capture_frame(ser), out, prefix="00"))

        _send(ser, "m")
        time.sleep(4.0)
        state = read_state(ser)
        if state.pages <= 0:
            raise RuntimeError("no deck pages available; run with --refresh or long-press/fetch on device")

        for i in range(state.pages):
            saved.append(_save_frame(capture_frame(ser), out, prefix=f"{i + 1:02d}"))
            if i != state.pages - 1:
                _send(ser, "n")
                time.sleep(4.0)

    sheet = _contact_sheet(saved, out)
    (out / "README.txt").write_text(
        "epd397 exact framebuffer capture\n\n"
        f"port: {port}\n"
        f"frames: {len(saved)}\n"
        "native/: 800x480 panel PNGs decoded from firmware 4-gray bytes\n"
        "contact-sheet.png: combined preview\n",
    )
    typer.echo(f"wrote {out}")
    typer.echo(f"contact sheet: {sheet}")
    if open_contact:
        subprocess.run(["open", str(sheet)], check=False)


if __name__ == "__main__":
    app()
