#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow", "numpy"]
# ///
"""Headless layout oracle for the rlcd (Waveshare ESP32-S3-RLCD-4.2) dashboard.

The panel is a 4.2" ST7305 **monochrome reflective** LCD, 400x300 landscape,
1bpp. Unlike the t-amoled AMOLED target there is NO white<->black swap: the
firmware composes black ink on a white field and the ST7305 push thresholds
each sprite pixel (white stays white, everything else -> black ink), mirroring
`mono_is_ink()` / `pack_mono_landscape()` in src/render/mono_pack.cpp.

So this oracle composes black-on-white and applies the SAME threshold, then
renders 1:1. It mirrors `render_dashboard()` (src/render/page.cpp) for LAYOUT
and hierarchy. Glyphs are approximated with a system sans (the firmware uses
TFT_eSPI FreeSans/FreeSansBold GFX fonts), so positions/sizes are truthful but
exact glyph edges are HIL-verified, not pixel-exact here.

    uv run firmware/rlcd/tools/preview_dashboard.py
    open /tmp/rlcd_dashboard.png
"""

from __future__ import annotations

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# --- device geometry (src/render/page.h) -----------------------------------
W, H = 400, 300
S = 3  # supersample device px -> SxS output, kept crisp via mono threshold

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

# --- fonts: approximate the FreeSans/FreeSansBold GFX ladder ----------------
# Firmware sizes (px cap-ish height) we target on-device:
#   hero temp  ~ FreeSansBold24pt @ textSize 2  (~66 px)
#   clock      ~ FreeSansBold18pt                (~26 px)
#   humidity   ~ FreeSansBold18pt @ textSize 1  (~25 px)
#   value sm   ~ FreeSansBold12pt                (~17 px)
#   label      ~ FreeSans9pt                     (~13 px)
_CANDIDATES_R = [
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
]
_CANDIDATES_B = [
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
]


def _load(cands: list[str], px: int) -> ImageFont.FreeTypeFont:
    for p in cands:
        try:
            return ImageFont.truetype(p, px * S)
        except OSError:
            continue
    return ImageFont.load_default()


def reg(px: int):
    return _load(_CANDIDATES_R, px)


def bold(px: int):
    return _load(_CANDIDATES_B, px)


class Canvas:
    """400x300 device-coord surface at S supersample, composed black-on-white."""

    def __init__(self):
        self.img = Image.new("RGB", (W * S, H * S), WHITE)
        self.d = ImageDraw.Draw(self.img)

    def line(self, x0, y0, x1, y1, t=1):
        self.d.line([x0 * S, y0 * S, x1 * S, y1 * S], fill=BLACK, width=t * S)

    def rect_outline(self, x, y, w, h, t=1):
        self.d.rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], outline=BLACK, width=t * S)

    def rect(self, x, y, w, h, color=BLACK):
        self.d.rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], fill=color)

    def ring(self, cx, cy, r, t=1):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], outline=BLACK, width=t * S)

    def text(self, x, y, s, font, anchor="la"):
        """y is the top (anchor uses 'a' = ascender top to match TL_DATUM)."""
        self.d.text((x * S, y * S), s, fill=BLACK, font=font, anchor=anchor)

    def text_w(self, s, font) -> int:
        return round(self.d.textlength(s, font=font) / S)

    def finalize(self) -> Image.Image:
        # mono threshold: only exact white stays white; all else -> black ink.
        a = np.asarray(self.img)
        ink = ~(a == 255).all(axis=2)
        out = np.full_like(a, 255)
        out[ink] = 0
        return Image.fromarray(out)


def draw_degree_c(c: Canvas, x: int, y_top: int, num_h: int):
    """Draw a degree ring + 'C' to the right of a `num_h`-tall number, top-aligned."""
    r = max(3, num_h // 12)
    cx = x + r + 2
    cy = y_top + r + 3
    c.ring(cx, cy, r, t=max(1, S))  # thickness handled by S in ring? keep simple
    cfont = bold(int(num_h * 0.42))
    c.text(cx + r + 4, y_top + int(num_h * 0.12), "C", cfont)


def render_dashboard(
    temp_c: float, rh: float, batt_pct: int, batt_v: float, clock: str, weekday: str, date: str, charging: bool
) -> Image.Image:
    c = Canvas()

    # ---- top status bar ----
    PAD = 14
    c.text(PAD, 8, "RCINK", bold(17))
    sub = bold(10)
    c.text(PAD, 30, "DESK CLIMATE", sub)
    # clock right-aligned; weekday-date small under it
    clk_f = bold(24)
    cw = c.text_w(clock, clk_f)
    c.text(W - PAD - cw, 6, clock, clk_f)
    dd = reg(11)
    ddtxt = f"{weekday}  {date}"
    c.text(W - PAD - c.text_w(ddtxt, dd), 45, ddtxt, dd)
    c.line(PAD, 66, W - PAD, 66, t=1)

    # ---- hero: temperature (centered) ----
    c.text(W // 2, 72, "TEMPERATURE", reg(11), anchor="ma")
    num = f"{temp_c:.1f}"
    num_h = 64
    nf = bold(num_h)
    nw = c.text_w(num, nf)
    # center the number+degree block as a unit
    deg_w = num_h // 2
    block_w = nw + deg_w + 8
    nx = (W - block_w) // 2
    ny = 86
    c.text(nx, ny, num, nf)
    draw_degree_c(c, nx + nw + 4, ny, num_h)

    # ---- humidity row ----
    hy = 174
    c.text(PAD, hy + 4, "HUMIDITY", reg(12))
    hf = bold(22)
    htxt = f"{rh:.0f}%"
    c.text(W - PAD - c.text_w(htxt, hf), hy + 2, htxt, hf)
    # RH bar
    by = hy + 40
    bx, bh = PAD + 4, 12
    bw = W - 2 * bx
    c.rect_outline(bx, by, bw, bh, t=1)
    fill = int((bw - 4) * max(0.0, min(100.0, rh)) / 100.0)
    if fill > 0:
        c.rect(bx + 2, by + 2, fill, bh - 4)

    # ---- bottom battery strip ----
    c.line(PAD, 250, W - PAD, 250, t=1)
    sy = 262
    c.text(PAD, sy + 2, "BATTERY", reg(12))
    bf = bold(13)
    vtxt = f"{batt_v:.2f}V"
    pct = f"{batt_pct}%"
    c.text(W - PAD - c.text_w(pct, bf), sy, pct, bf)
    c.text(W - PAD - c.text_w(pct, bf) - 12 - c.text_w(vtxt, reg(12)), sy + 1, vtxt, reg(12))
    # battery bar starts after the measured label (mirrors render_dashboard)
    blx = PAD + c.text_w("BATTERY", reg(12)) + 12
    brx = W - PAD - c.text_w(pct, bf) - 12 - c.text_w(vtxt, reg(12)) - 12
    bby, bbh = sy + 1, 14
    if brx - blx > 30:
        c.rect_outline(blx, bby, brx - blx, bbh, t=1)
        f = int((brx - blx - 4) * max(0, min(100, batt_pct)) / 100)
        if f > 0:
            c.rect(blx + 2, bby + 2, f, bbh - 4)

    return c.finalize()


def main():
    img = render_dashboard(
        temp_c=28.4,
        rh=64,
        batt_pct=87,
        batt_v=4.01,
        clock="14:32",
        weekday="WED",
        date="JUN 12",
        charging=False,
    )
    big = img.resize((W * S, H * S), Image.NEAREST)
    big.save("/tmp/rlcd_dashboard.png")
    print("wrote /tmp/rlcd_dashboard.png", big.size)

    # contact sheet: a few states side by side
    states = [
        (
            "normal",
            dict(
                temp_c=28.4,
                rh=64,
                batt_pct=87,
                batt_v=4.01,
                clock="14:32",
                weekday="WED",
                date="JUN 12",
                charging=False,
            ),
        ),
        (
            "cold/dry/low",
            dict(
                temp_c=8.0, rh=22, batt_pct=12, batt_v=3.51, clock="06:08", weekday="MON", date="JAN 02", charging=False
            ),
        ),
        (
            "hot/humid/full",
            dict(
                temp_c=34.7,
                rh=95,
                batt_pct=100,
                batt_v=4.19,
                clock="23:59",
                weekday="SUN",
                date="AUG 31",
                charging=True,
            ),
        ),
    ]
    pad, gap, top = 20, 24, 44
    pw, ph = W * S, H * S
    sheet = Image.new("RGB", (pad * 2 + len(states) * pw + (len(states) - 1) * gap, top + ph + pad), (28, 28, 28))
    d = ImageDraw.Draw(sheet)
    lf = ImageFont.truetype(_CANDIDATES_R[-1], 22)
    d.text((pad, 14), "rlcd dashboard  400x300  (mono reflective, black-on-white)", fill=(255, 255, 255), font=lf)
    for i, (name, kw) in enumerate(states):
        x = pad + i * (pw + gap)
        d.text((x, top - 22), name, fill=(200, 200, 200), font=lf)
        sheet.paste(render_dashboard(**kw).resize((pw, ph), Image.NEAREST), (x, top))
    sheet.save("/tmp/rlcd_dashboard_states.png")
    print("wrote /tmp/rlcd_dashboard_states.png", sheet.size)


if __name__ == "__main__":
    main()
