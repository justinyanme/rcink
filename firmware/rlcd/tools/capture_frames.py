#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow", "pyserial", "typer"]
# ///
"""Capture exact rlcd screenshots from the firmware framebuffer dump.

Requires firmware with the console `p` command. The command prints the last
packed ST7305 400x300 1bpp frame; this script decodes that native buffer into
PNG files, so screenshots match the real firmware renderer and font stack.

    uv run tools/capture_frames.py --port /dev/cu.usbmodem31301 --open
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

SCREEN_W = 400
SCREEN_H = 300
FRAME_BYTES = (SCREEN_W * SCREEN_H) // 8

BEGIN_RE = re.compile(
    r"^\[frame\] begin w=(?P<w>\d+) h=(?P<h>\d+) bytes=(?P<bytes>\d+) "
    r"mode=(?P<mode>\S+) idx=(?P<idx>\d+) count=(?P<count>\d+) "
    r"id=(?P<id>\S+) polarity=(?P<polarity>\S+)$"
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
    polarity: str


@dataclass(slots=True)
class DeviceState:
    mode: str
    pages: int
    idx: int


def unpack_st7305_frame(data: bytes, *, width: int = SCREEN_W, height: int = SCREEN_H) -> Image.Image:
    expected = (width * height) // 8
    if len(data) != expected:
        raise ValueError(f"bad frame length: got {len(data)}, expected {expected}")
    if width % 2 != 0 or height % 4 != 0:
        raise ValueError(f"unsupported ST7305 geometry: {width}x{height}")

    img = Image.new("RGB", (width, height), "white")
    px = img.load()
    blocks_y = height // 4
    for y in range(height):
        inv_y = height - 1 - y
        block_y = inv_y >> 2
        local_y = inv_y & 3
        for x in range(width):
            idx = (x >> 1) * blocks_y + block_y
            bit = 7 - ((local_y << 1) | (x & 1))
            if (data[idx] & (1 << bit)) == 0:
                px[x, y] = (0, 0, 0)
    return img


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
            return DeviceState(
                mode=m.group("mode"),
                pages=int(m.group("pages")),
                idx=int(m.group("idx")),
            )
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


def capture_frame(ser: serial.Serial, *, timeout_s: float = 12.0) -> FrameDump:
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
            w = int(meta.group("w"))
            h = int(meta.group("h"))
            n = int(meta.group("bytes"))
            if len(data) != n:
                raise RuntimeError(f"frame length mismatch: got {len(data)}, expected {n}")
            return FrameDump(
                image=unpack_st7305_frame(data, width=w, height=h),
                mode=meta.group("mode"),
                page_id=meta.group("id"),
                idx=int(meta.group("idx")),
                count=int(meta.group("count")),
                polarity=meta.group("polarity"),
            )
        if HEX_RE.fullmatch(line):
            chunks.append(line)
    tail = "\n".join(seen[-12:])
    raise RuntimeError(f"frame dump timed out; tail:\n{tail}")


def _safe_name(s: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "-", s).strip("-") or "screen"


def _save_frame(frame: FrameDump, out_dir: Path, *, prefix: str) -> Path:
    native = out_dir / "native"
    big = out_dir / "preview-3x"
    native.mkdir(parents=True, exist_ok=True)
    big.mkdir(parents=True, exist_ok=True)
    name = f"{prefix}-{_safe_name(frame.page_id)}.png"
    native_path = native / name
    frame.image.save(native_path)
    frame.image.resize((SCREEN_W * 3, SCREEN_H * 3), Image.Resampling.NEAREST).save(big / name)
    return native_path


def _font(px: int):
    for path in (
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    ):
        try:
            return ImageFont.truetype(path, px)
        except OSError:
            continue
    return ImageFont.load_default()


def _contact_sheet(paths: list[Path], out: Path) -> Path:
    cols = 2
    cell_w, cell_h = 820, 650
    rows = (len(paths) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell_w, 54 + rows * cell_h), (32, 32, 32))
    draw = ImageDraw.Draw(sheet)
    font = _font(18)
    label_font = _font(12)
    draw.text(
        (20, 16),
        f"rlcd exact framebuffer capture  {dt.datetime.now().astimezone().isoformat()}",
        fill="white",
        font=font,
    )
    for i, path in enumerate(paths):
        x = (i % cols) * cell_w + 20
        y = 54 + (i // cols) * cell_h
        draw.text((x, y), path.stem, fill=(220, 220, 220), font=label_font)
        img = Image.open(path).resize((800, 600), Image.Resampling.NEAREST)
        sheet.paste(img, (x, y + 22))
    out.mkdir(parents=True, exist_ok=True)
    path = out / "contact-sheet.png"
    sheet.save(path)
    return path


@app.command()
def main(
    port: Annotated[
        str,
        typer.Option(
            "--port",
            help="Verified rlcd USB-CDC port; required because port suffixes drift across devices.",
        ),
    ],
    baud: Annotated[int, typer.Option("--baud")] = 115200,
    out: Annotated[
        Path | None,
        typer.Option("--out", help="Output directory. Default: /tmp/rlcd-capture-<timestamp>."),
    ] = None,
    refresh: Annotated[
        bool,
        typer.Option("--refresh/--no-refresh", help="Fetch fresh Axiom pages before capture."),
    ] = True,
    open_contact: Annotated[bool, typer.Option("--open", help="Open contact-sheet.png after capture.")] = False,
) -> None:
    out = out or Path("/tmp") / ("rlcd-capture-" + dt.datetime.now().strftime("%Y%m%d-%H%M%S"))
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
            time.sleep(1.5)

        _send(ser, "t")
        time.sleep(1.5)
        saved.append(_save_frame(capture_frame(ser), out, prefix="00"))

        _send(ser, "m")
        time.sleep(1.5)
        state = read_state(ser)
        if state.pages <= 0:
            raise RuntimeError("no deck pages available; run with --refresh or long-press/fetch on device")

        for i in range(state.pages):
            saved.append(_save_frame(capture_frame(ser), out, prefix=f"{i + 1:02d}"))
            if i != state.pages - 1:
                _send(ser, "n")
                time.sleep(1.5)

    sheet = _contact_sheet(saved, out)
    (out / "README.txt").write_text(
        "rlcd exact framebuffer capture\n\n"
        f"port: {port}\n"
        f"frames: {len(saved)}\n"
        "native/: 400x300 panel PNGs decoded from firmware ST7305 bytes\n"
        "preview-3x/: 1200x900 scaled copies\n"
        "contact-sheet.png: combined preview\n",
    )
    typer.echo(f"wrote {out}")
    typer.echo(f"contact sheet: {sheet}")
    if open_contact:
        subprocess.run(["open", str(sheet)], check=False)


if __name__ == "__main__":
    app()
