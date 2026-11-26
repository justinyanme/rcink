#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["freetype-py", "httpx"]
# ///
"""Bake Barlow + Barlow Semi Condensed (SIL OFL 1.1) into 1-bit GFXfont sources.

Rasterizes printable ASCII (0x20..0x7E) using FreeType's light hinter + coverage
thresholding, trims empty margins, optionally equalizes digit advances for
tabular numerals, and emits:
  src/render/rlcd_fonts.h
  src/render/rlcd_fonts.cpp

Run from firmware/rlcd/:
  uv run tools/bake_rlcd_fonts.py           # regenerate headers/sources
  uv run tools/bake_rlcd_fonts.py --check   # fail if generated files are stale
  uv run tools/bake_rlcd_fonts.py --metrics # print measured bounds for key strings
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

import freetype
import httpx

ROOT = Path(__file__).resolve().parent.parent
CACHE_DIR = ROOT / ".pio" / "font-cache"
OUT_H = ROOT / "src" / "render" / "rlcd_fonts.h"
OUT_CPP = ROOT / "src" / "render" / "rlcd_fonts.cpp"

BASE_URL = "https://raw.githubusercontent.com/jpt/barlow/main/fonts/ttf"
FIRST = 0x20
LAST = 0x7E


@dataclass(frozen=True)
class FontSpec:
    symbol: str
    ttf_file: str
    px_size: int
    thresh: int
    extra_track: int = 0
    space_extra: int = 0
    tabular_digits: bool = False


SPECS = [
    FontSpec("kBarlowChip15", "Barlow-Bold.ttf", 15, thresh=116, extra_track=1, space_extra=1, tabular_digits=True),
    FontSpec("kBarlowBody18", "Barlow-Medium.ttf", 18, thresh=118, space_extra=1, tabular_digits=True),
    FontSpec("kBarlowBodyBold18", "Barlow-Bold.ttf", 18, thresh=105, space_extra=1, tabular_digits=True),
    FontSpec("kBarlowName24", "Barlow-Bold.ttf", 24, thresh=122, space_extra=1, tabular_digits=True),
    FontSpec("kBarlowHero36", "BarlowSemiCondensed-Bold.ttf", 36, thresh=124, tabular_digits=True),
    FontSpec("kBarlowHero48", "BarlowSemiCondensed-Bold.ttf", 48, thresh=124, tabular_digits=True),
]


def ensure_ttf(filename: str) -> Path:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    path = CACHE_DIR / filename
    if path.exists():
        return path
    url = f"{BASE_URL}/{filename}"
    print(f"fetching {url} -> {path}", file=sys.stderr)
    r = httpx.get(url, timeout=30, follow_redirects=True)
    r.raise_for_status()
    path.write_bytes(r.content)
    return path


@dataclass
class BakedGlyph:
    code: int
    bitmap_offset: int
    width: int
    height: int
    x_advance: int
    x_offset: int
    y_offset: int
    rows: list[list[bool]]


def bake_font(spec: FontSpec) -> tuple[list[int], list[BakedGlyph], int]:
    ttf = ensure_ttf(spec.ttf_file)
    face = freetype.Face(str(ttf))
    face.set_pixel_sizes(0, spec.px_size)

    raw_glyphs: list[dict] = []
    for code in range(FIRST, LAST + 1):
        face.load_char(chr(code), freetype.FT_LOAD_DEFAULT | freetype.FT_LOAD_TARGET_LIGHT)
        face.glyph.render(freetype.FT_RENDER_MODE_NORMAL)
        bmp = face.glyph.bitmap
        w, h, pitch, buf = bmp.width, bmp.rows, bmp.pitch, bmp.buffer
        rows = [[buf[y * pitch + x] >= spec.thresh for x in range(w)] for y in range(h)]

        top, bot, left, right = 0, h, 0, w
        while top < bot and not any(rows[top]):
            top += 1
        while bot > top and not any(rows[bot - 1]):
            bot -= 1
        while left < right and not any(rows[y][left] for y in range(top, bot)):
            left += 1
        while right > left and not any(rows[y][right - 1] for y in range(top, bot)):
            right -= 1

        tw = (right - left) if bot > top else 0
        th = bot - top
        trimmed = [r[left:right] for r in rows[top:bot]] if tw and th else []
        x_advance = ((face.glyph.advance.x + 32) >> 6) + spec.extra_track + (
            spec.space_extra if code == 0x20 else 0
        )
        x_offset = face.glyph.bitmap_left + (left if tw else 0)
        y_offset = 1 - face.glyph.bitmap_top + (top if th else 0)

        raw_glyphs.append(
            {
                "code": code,
                "width": tw,
                "height": th,
                "x_advance": max(0, min(255, x_advance)),
                "x_offset": max(-128, min(127, x_offset)),
                "y_offset": max(-128, min(127, y_offset)),
                "rows": trimmed,
            }
        )

    if spec.tabular_digits:
        digits = [g for g in raw_glyphs if ord("0") <= g["code"] <= ord("9")]
        max_adv = max(g["x_advance"] for g in digits)
        for g in digits:
            diff = max_adv - g["x_advance"]
            g["x_offset"] += diff // 2
            g["x_advance"] = max_adv

    bitmaps: list[int] = []
    glyphs: list[BakedGlyph] = []
    for g in raw_glyphs:
        offset = len(bitmaps)
        cur = 0
        bit = 0
        for row in g["rows"]:
            for on in row:
                if on:
                    cur |= 0x80 >> bit
                bit += 1
                if bit == 8:
                    bitmaps.append(cur)
                    cur = 0
                    bit = 0
        if bit > 0:
            bitmaps.append(cur)

        glyphs.append(
            BakedGlyph(
                code=g["code"],
                bitmap_offset=offset,
                width=g["width"],
                height=g["height"],
                x_advance=g["x_advance"],
                x_offset=g["x_offset"],
                y_offset=g["y_offset"],
                rows=g["rows"],
            )
        )

    y_advance = max(1, min(255, (face.size.height + 32) >> 6))
    return bitmaps, glyphs, y_advance


def render_files() -> tuple[str, str, dict[str, tuple[list[int], list[BakedGlyph], int]]]:
    baked: dict[str, tuple[list[int], list[BakedGlyph], int]] = {}
    for spec in SPECS:
        baked[spec.symbol] = bake_font(spec)

    h_lines = [
        "// AUTO-GENERATED by tools/bake_rlcd_fonts.py. Do not edit by hand.",
        "// Source: Barlow & Barlow Semi Condensed (SIL Open Font License 1.1).",
        "#pragma once",
        "#ifdef ARDUINO",
        "#include <TFT_eSPI.h>",
        "",
        "namespace rcpaper::rlcd_fonts {",
        "",
    ]
    for spec in SPECS:
        h_lines.append(f"extern const GFXfont {spec.symbol};")
    h_lines += ["", "}  // namespace rcpaper::rlcd_fonts", "#endif  // ARDUINO", ""]

    cpp_lines = [
        "// AUTO-GENERATED by tools/bake_rlcd_fonts.py. Do not edit by hand.",
        "// Source: Barlow & Barlow Semi Condensed (SIL Open Font License 1.1).",
        "#ifdef ARDUINO",
        '#include "render/rlcd_fonts.h"',
        "",
        "namespace rcpaper::rlcd_fonts {",
        "",
    ]
    for spec in SPECS:
        bitmaps, glyphs, y_adv = baked[spec.symbol]
        cpp_lines.append(
            f"// {spec.ttf_file} @ {spec.px_size}px (thresh={spec.thresh}, "
            f"track=+{spec.extra_track}, tabular={int(spec.tabular_digits)})"
        )
        cpp_lines.append(f"static const uint8_t {spec.symbol}Bitmaps[] PROGMEM = {{")
        for i in range(0, len(bitmaps), 12):
            chunk = ", ".join(f"0x{b:02X}" for b in bitmaps[i : i + 12])
            cpp_lines.append(f"    {chunk},")
        cpp_lines.append("};")
        cpp_lines.append("")
        cpp_lines.append(f"static const GFXglyph {spec.symbol}Glyphs[] PROGMEM = {{")
        for g in glyphs:
            ch = chr(g.code)
            if ch in ("\\", "'", '"'):
                ch = "\\" + ch
            cpp_lines.append(
                f"    {{ {g.bitmap_offset:5d}, {g.width:3d}, {g.height:3d}, "
                f"{g.x_advance:3d}, {g.x_offset:4d}, {g.y_offset:4d} }},  "
                f"// 0x{g.code:02X} '{ch}'"
            )
        cpp_lines.append("};")
        cpp_lines.append("")
        cpp_lines.append(f"extern const GFXfont {spec.symbol} PROGMEM = {{")
        cpp_lines.append(f"    (uint8_t*){spec.symbol}Bitmaps,")
        cpp_lines.append(f"    (GFXglyph*){spec.symbol}Glyphs,")
        cpp_lines.append(f"    0x{FIRST:02X}, 0x{LAST:02X}, {y_adv},")
        cpp_lines.append("};")
        cpp_lines.append("")
    cpp_lines += ["}  // namespace rcpaper::rlcd_fonts", "#endif  // ARDUINO", ""]

    return "\n".join(h_lines), "\n".join(cpp_lines), baked


def measure(glyphs: list[BakedGlyph], s: str) -> tuple[int, int, int]:
    adv = 0
    min_y, max_y = 999, -999
    for ch in s:
        idx = ord(ch) - FIRST
        if 0 <= idx < len(glyphs):
            g = glyphs[idx]
            if g.height > 0:
                min_y = min(min_y, g.y_offset)
                max_y = max(max_y, g.y_offset + g.height)
            adv += g.x_advance
    if min_y == 999:
        min_y, max_y = 0, 0
    return adv, min_y, max_y


def print_metrics(baked: dict[str, tuple[list[int], list[BakedGlyph], int]]) -> None:
    samples = {
        "kBarlowChip15": ["HOST", "ACTIVE", "ECO", "SYNC FAILED", "OLD DATA", "NO WIFI", "NO NTP", "NO CLOCK", "SYNCING", "3h old"],
        "kBarlowBody18": [
            "6d 23h", "retry 23:59", "retry ~30m", "waiting for NTP", "fetching now",
            "checked 13:38", "since Sep '25", "100%", "~98% to spare", "7 days", "ODO",
            "today 999M", "999%+ of a usual day", "Best bet today:",
        ],
        "kBarlowBodyBold18": ["dry 47h early", "999B", "Codex is on a tear today", "Busiest this week: Gemini", "Z.AI"],
        "kBarlowName24": ["Claude", "Codex", "Gemini", "Kimi", "Z.AI", "%"],
        "kBarlowHero36": ["99.9M", "2.96B", "3.6B"],
        "kBarlowHero48": ["100", "96", "70", "99", "99.9M", "2.96B", "--"],
    }
    total_bytes = 0
    for spec in SPECS:
        bitmaps, glyphs, _ = baked[spec.symbol]
        sz = len(bitmaps) + len(glyphs) * 8 + 12
        total_bytes += sz
        print(f"=== {spec.symbol} (~{sz} B flash) ===")
        for s in samples[spec.symbol]:
            adv, min_y, max_y = measure(glyphs, s)
            print(f"  {s!r:30s} adv={adv:3d} y=[{min_y:3d},{max_y:2d})")
    print(f"Total estimated flash: {total_bytes} B")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--metrics", action="store_true")
    args = parser.parse_args()

    h_text, cpp_text, baked = render_files()
    if args.metrics:
        print_metrics(baked)

    if args.check:
        stale = (
            not OUT_H.exists()
            or OUT_H.read_text() != h_text
            or not OUT_CPP.exists()
            or OUT_CPP.read_text() != cpp_text
        )
        if stale:
            print("src/render/rlcd_fonts.{h,cpp} are stale; rerun tools/bake_rlcd_fonts.py", file=sys.stderr)
            return 1
        return 0

    OUT_H.write_text(h_text)
    OUT_CPP.write_text(cpp_text)
    print(f"wrote {OUT_H.relative_to(ROOT)} and {OUT_CPP.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
