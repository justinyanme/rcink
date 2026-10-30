#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pillow", "numpy"]
# ///
"""Headless oracle for the t-amoled right sidebar / page-indicator redesign.

Mirrors `paint_sidebar()` (src/render/page.cpp) pixel-for-pixel: composes the
scene in LIGHT (black ink on white, hard 1-bit edges, real 6x8 / 12x16 GLCD cell
metrics) and then applies the SAME exact 0xFFFF<->0x0000 swap that
`push_canvas()` does — so a mark only "lights up" on the AMOLED black if it is
Color::Black (->white) or a saturated accent (passes through). Anything painted
pure white inverts to black = invisible. This makes the mockups truthful, not
just pretty.

Renders the current baseline ("2/3" text) plus three redesign candidates that
fold the page indicator into the divider, and writes two contact sheets:
  /tmp/sidebar_context.png  - full 536x240 device, baseline + 3 candidates
  /tmp/sidebar_scaling.png  - sidebar strips across deck sizes / positions

    uv run firmware/t-amoled/tools/preview_sidebar.py
"""

from __future__ import annotations

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# --- device geometry (src/render/page.h) ----------------------------------
W, H = 536, 240
SIDEBAR_W = 56
X0 = W - SIDEBAR_W  # 480, divider x
CX = X0 + 6  # 486, text inset

# --- palette (render/palette.h), pre-swap (light) --------------------------
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (0xFF, 0x33, 0x33)
YELLOW = (0xFF, 0xC5, 0x33)
GREEN = (0x30, 0xD1, 0x58)
BLUE = (0x3B, 0x82, 0xF6)

S = 4  # supersample: device px -> SxS output block (nearest, so it stays crisp)

FONT_PATH = "/System/Library/Fonts/Monaco.ttf"
_glyph_cache: dict[tuple, np.ndarray] = {}
_big_font = ImageFont.truetype(FONT_PATH, 96)


def _glyph_mask(ch: str, gw: int, gh: int) -> np.ndarray:
    """1-bit glyph (gh,gw bool) — render big, crop to ink, fit the GLCD glyph
    box. Honours fixed-cell advance so the vertical/text budget stays exact."""
    key = (ch, gw, gh)
    cached = _glyph_cache.get(key)
    if cached is not None:
        return cached
    if ch == " ":
        m = np.zeros((gh, gw), dtype=bool)
        _glyph_cache[key] = m
        return m
    big = Image.new("L", (160, 160), 0)
    ImageDraw.Draw(big).text((20, 10), ch, fill=255, font=_big_font)
    bbox = big.getbbox()
    glyph = big.crop(bbox).resize((gw, gh), Image.LANCZOS)
    m = np.asarray(glyph) > 110
    _glyph_cache[key] = m
    return m


class Canvas:
    """Device-coordinate draw surface at S supersample. Light composition."""

    def __init__(self):
        self.img = Image.new("RGB", (W * S, H * S), WHITE)
        self.d = ImageDraw.Draw(self.img)
        self.arr = None  # filled by glyph blits via numpy at finalize

    # -- primitives (device coords) --
    def rect(self, x, y, w, h, color):
        self.d.rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], fill=color)

    def rect_outline(self, x, y, w, h, color, t=1):
        self.d.rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], outline=color, width=t * S)

    def vline(self, x, y, h, color, t=1):
        self.rect(x, y, t, h, color)

    def disc(self, cx, cy, r, color):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=color)

    def ring(self, cx, cy, r, color, t=1):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], outline=color, width=t * S)

    def tri(self, pts, color):
        self.d.polygon([(px * S, py * S) for px, py in pts], fill=color)

    def text(self, x, y, s, size, color):
        """GLCD font: 6x8 cell at size 1 (5x7 glyph), integer scale by `size`."""
        cw, _ch = 6 * size, 8 * size
        gw, gh = 5 * size * S, 7 * size * S
        base = np.asarray(self.img).copy()
        for i, c in enumerate(s):
            m = _glyph_mask(c, gw, gh)
            px = (x + i * cw) * S
            py = y * S
            sub = base[py : py + gh, px : px + gw]
            if sub.shape[:2] == m.shape:
                sub[m] = color
        self.img = Image.fromarray(base)
        self.d = ImageDraw.Draw(self.img)

    # -- finalize: same swap as push_canvas() --
    def to_dark(self) -> Image.Image:
        a = np.asarray(self.img).copy()
        white = (a == 255).all(axis=2)
        black = (a == 0).all(axis=2)
        a[white] = BLACK
        a[black] = WHITE
        return Image.fromarray(a)


# --- representative content (left of the sidebar) --------------------------
def draw_content(c: Canvas):
    c.text(16, 14, "CLAUDE CODE", 2, BLACK)
    c.text(16, 40, "82%", 6, BLACK)
    c.text(190, 64, "USED", 2, BLACK)
    c.text(16, 104, "OPUS 4.8  *  5H 12M LEFT", 1, BLACK)
    # hbar progress
    c.rect_outline(16, 124, 300, 14, BLACK, 1)
    c.rect(18, 126, int(296 * 0.82), 10, BLUE)
    # sparkline bars
    heights = [6, 9, 7, 12, 10, 15, 13, 18, 16, 22, 19, 24, 21, 17, 20, 26, 23, 28, 25, 30, 27, 24, 29, 33]
    bx, base_y = 16, 214
    for i, hgt in enumerate(heights):
        c.rect(bx + i * 18, base_y - hgt, 12, hgt, GREEN)


# --- sidebar chrome shared by every candidate (status + nav) ---------------
def draw_sidebar_chrome(c: Canvas, batt_pct=78):
    c.rect(X0, 0, SIDEBAR_W, H, WHITE)  # sidebar bg (->black)
    c.vline(X0, 0, H, BLACK, 1)  # divider (->white) baseline
    # battery
    c.rect_outline(CX, 8, 28, 12, BLACK, 1)
    c.rect(CX + 28, 12, 2, 4, BLACK)
    fill = GREEN if batt_pct >= 30 else (YELLOW if batt_pct >= 10 else RED)
    fw = int((28 - 4) * batt_pct / 100)
    if fw:
        c.rect(CX + 2, 10, fw, 8, fill)
    c.text(CX, 24, f"{batt_pct}%", 1, BLACK)
    # weekday / date / clock
    c.text(CX, 38, "THU", 1, BLACK)
    c.text(CX, 50, "JUN 11", 1, BLACK)
    c.text(CX, 62, "09:30", 1, BLACK)
    # nav hints by the two buttons
    prev_y, next_y = H - 58, H - 26
    c.tri([(CX + 4, prev_y), (CX, prev_y + 7), (CX + 8, prev_y + 7)], BLACK)
    c.text(CX + 13, prev_y + 1, "PREV", 1, BLACK)
    c.tri([(CX, next_y), (CX + 8, next_y), (CX + 4, next_y + 7)], BLACK)
    c.text(CX + 13, next_y, "NEXT", 1, BLACK)


# --- the four indicator treatments -----------------------------------------
# Slot the "2/3" text used: status block, y in [78, 96).
RAIL_TOP, RAIL_BOT = 8, H - 64  # 8..176, clear of the PREV/NEXT block


def ind_baseline(c: Canvas, n, idx):
    c.text(CX, 78, f"{idx + 1}/{n}", 2, BLACK)


def ind_thumb(c: Canvas, n, idx):
    # Scrollbar thumb folded onto the divider: thin full track + a fat bright
    # thumb covering the current page's vertical fraction. White (->lit) thumb.
    top, bot = RAIL_TOP, RAIL_BOT
    track = bot - top
    th = max(6, track // n)  # integer floor, mirrors firmware
    ty = top + (track * idx) // n  # integer floor, mirrors firmware
    if ty + th > bot:
        ty = bot - th
    c.rect(X0 - 2, ty, 5, th, BLACK)  # fat thumb straddling the divider


def ind_segmented(c: Canvas, n, idx):
    # Divider split into N pills with gaps. Current = blue accent (pops without
    # leaning on brightness), others = plain white line segment.
    top, bot = RAIL_TOP, RAIL_BOT
    gap = 4
    seg = (bot - top - (n - 1) * gap) / n
    for i in range(n):
        y = top + i * (seg + gap)
        if i == idx:
            c.rect(X0 - 2, round(y), 5, round(seg), BLUE)
        else:
            c.vline(X0, round(y), round(seg), BLACK, 1)


def ind_dots(c: Canvas, n, idx):
    # Vertical dot column in the freed status slot. Current = filled white disc,
    # others = hollow white ring (filled-vs-hollow, not brightness).
    col_x = X0 + SIDEBAR_W // 2  # 508
    pitch = 9
    y0 = 86
    for i in range(n):
        cy = y0 + i * pitch
        if i == idx:
            c.disc(col_x, cy, 3, BLACK)
        else:
            c.ring(col_x, cy, 3, BLACK, 1)


CANDIDATES = [
    ("Baseline  (current  2/3 text)", ind_baseline),
    ("A. Rail thumb  (scrollbar on divider)", ind_thumb),
    ("B. Segmented rail  (N pills, current=blue)", ind_segmented),
    ("C. Dot column  (filled=current, hollow=rest)", ind_dots),
]


def build_panel(indicator, n, idx, with_content=True) -> Image.Image:
    c = Canvas()
    if with_content:
        draw_content(c)
    draw_sidebar_chrome(c)
    indicator(c, n, idx)
    return c.to_dark()


# --- contact-sheet composition (annotations are NOT device pixels) ---------
BG = (24, 24, 24)
LABEL = (200, 200, 200)
_lab_font = ImageFont.truetype(FONT_PATH, 22)
_hdr_font = ImageFont.truetype(FONT_PATH, 26)


def label(d, xy, text, font=_lab_font, color=LABEL):
    d.text(xy, text, fill=color, font=font)


def compose_context():
    """2x2 grid of full 536x240 devices: baseline + 3 candidates, deck 2/3."""
    pad, gap, top = 24, 28, 70
    pw, ph = W * S, H * S
    cols, rows = 2, 2
    sheet_w = pad * 2 + cols * pw + (cols - 1) * gap
    sheet_h = top + pad + rows * (ph + 34) + (rows - 1) * gap
    sheet = Image.new("RGB", (sheet_w, sheet_h), BG)
    d = ImageDraw.Draw(sheet)
    label(
        d,
        (pad, 24),
        "t-amoled  sidebar / page-indicator  -  deck position 2 of 3",
        font=_hdr_font,
        color=(255, 255, 255),
    )
    for k, (name, fn) in enumerate(CANDIDATES):
        r, col = divmod(k, cols)
        x = pad + col * (pw + gap)
        y = top + r * (ph + 34 + gap)
        label(d, (x, y), name)
        sheet.paste(build_panel(fn, 3, 1), (x, y + 30))
    sheet.save("/tmp/sidebar_context.png")
    print("wrote /tmp/sidebar_context.png", sheet.size)


def compose_scaling():
    """Rows = candidates, cols = scenarios; each cell is the sidebar strip only
    so position-encoding + scaling are legible side by side."""
    scenarios = [(3, 0, "1/3 top"), (3, 1, "2/3 mid"), (3, 2, "3/3 bot"), (6, 3, "4/6")]
    crop_x = 466
    sw = (W - crop_x) * S  # strip width
    sh = H * S
    pad, gap, top, lblw = 24, 22, 64, 360
    sheet_w = pad * 2 + lblw + len(scenarios) * (sw + gap)
    sheet_h = top + pad + len(CANDIDATES) * (sh + gap)
    sheet = Image.new("RGB", (sheet_w, sheet_h), BG)
    d = ImageDraw.Draw(sheet)
    label(
        d,
        (pad, 22),
        "position + scaling  (sidebar strip; cols = deck size / current page)",
        font=_hdr_font,
        color=(255, 255, 255),
    )
    for ci, (_, _, sc) in enumerate(scenarios):
        x = pad + lblw + ci * (sw + gap)
        label(d, (x, top - 30), sc)
    for ri, (name, fn) in enumerate(CANDIDATES):
        y = top + ri * (sh + gap)
        label(d, (pad, y + sh // 2 - 12), name.split("  ")[0])
        for ci, (n, idx, _) in enumerate(scenarios):
            x = pad + lblw + ci * (sw + gap)
            panel = build_panel(fn, n, idx, with_content=False)
            strip = panel.crop((crop_x * S, 0, W * S, H * S))
            sheet.paste(strip, (x, y))
    sheet.save("/tmp/sidebar_scaling.png")
    print("wrote /tmp/sidebar_scaling.png", sheet.size)


if __name__ == "__main__":
    compose_context()
    compose_scaling()
