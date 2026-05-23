# rcink golden PNG harness — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Python reference renderer that takes a DSL page + a device profile and produces a PNG. Use it to generate per-(device,page) goldens checked into `dsl/golden/<device>/<page>.png`. CI validates that every producer page renders without errors at every supported device viewport.

**Architecture:** A new module `producer/rcink/preview_png.py` exposes `render_to_png(layout, device) -> PIL.Image`. It reuses the layout math from `preview.py` (HTML) but draws to a PIL `Image` instead of emitting HTML. A `Device` dataclass captures viewport (`width`, `height`) and palette resolver (`token → "#RRGGBB"`). Three device profiles are defined: `s3` (400×600, 6-color), `c3-x4` (800×480, B&W), `tlsr` (250×128, BWR). The palette resolvers MUST match each firmware's `palette.cpp`/`palette.c` table exactly — drift between Python reference and firmware = test failure once each firmware has a host_test.

**Tech Stack:** Python ≥3.13, Pillow (PIL), pytest. PIL is the only new dep.

**Reference spec:** `docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md` §"Testing" and §"Capability model."

---

## Pre-flight

- [ ] **Step P.1: Confirm pytest baseline green**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest -q 2>&1 | tail -3
```
Expected: 159 passed.

- [ ] **Step P.2: Confirm pillow not already a dep**

```bash
cd ~/w/_hw/rcink/producer && uv pip list 2>&1 | grep -i pillow || echo "not installed"
```
Expected: `not installed`. We'll add it.

---

## Phase 1 — Device profiles

### Task 1.1: Define Device dataclass and three profiles

**Files:**
- Create: `producer/rcink/devices_profile.py`

This file holds the rendering-side device profiles (separate from `devices.py` which holds the *deployment-side* registry of physical units; they answer different questions).

- [ ] **Step 1.1.1: Write devices_profile.py**

Path: `producer/rcink/devices_profile.py`

```python
"""Rendering-side device profiles.

Each profile captures a panel's viewport (px) and palette resolver. The
producer uses these only for rendering goldens; on the wire the DSL stays
device-agnostic. Each firmware MUST mirror its profile's palette table
in its own palette.c / palette.cpp — divergence is a bug caught by golden
diffs.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from rcink.palette import E6_HEX, normalize_color


PaletteResolver = Callable[[str], str]
"""Map a DSL color token to a `#RRGGBB` string. Unknown token → black."""


@dataclass(frozen=True, slots=True)
class DeviceProfile:
    name: str
    width: int
    height: int
    resolve_color: PaletteResolver
    # Font registry: token → concrete pixel height. Mirrors each firmware's
    # font registry. Used by the reference renderer for text measurement.
    sizes: dict[str, int]


def _resolve_s3(token: str) -> str:
    """s3 is 6-ink Spectra. Semantic tokens map to their natural ink."""
    c = normalize_color(token)
    if c is None:
        return E6_HEX["black"]
    return E6_HEX[c]  # E6_HEX already encodes the s3-column mapping


def _resolve_c3_x4(token: str) -> str:
    """c3-x4 is B&W. Only `white` remains white; everything else is black."""
    c = normalize_color(token)
    if c == "white":
        return "#FFFFFF"
    return "#000000"


# tlsr (BWR) physical inks
_TLSR_BLACK = "#000000"
_TLSR_WHITE = "#FFFFFF"
_TLSR_RED = "#FF0000"

_TLSR_TABLE: dict[str, str] = {
    "black": _TLSR_BLACK,
    "white": _TLSR_WHITE,
    "red": _TLSR_RED,
    # legacy concrete: collapse to BWR per firmware palette.c
    "yellow": _TLSR_BLACK,
    "green": _TLSR_BLACK,
    "blue": _TLSR_RED,
    # semantic
    "accent": _TLSR_RED,
    "warning": _TLSR_RED,
    "success": _TLSR_BLACK,
    "info": _TLSR_BLACK,
    "muted": _TLSR_BLACK,
}


def _resolve_tlsr(token: str) -> str:
    c = normalize_color(token)
    if c is None:
        return _TLSR_BLACK
    return _TLSR_TABLE.get(c, _TLSR_BLACK)


# Font registries — mirror each firmware's px settings per the design spec.
_S3_SIZES: dict[str, int] = {"H1": 60, "H2": 32, "Body": 20, "Sm": 14,
                              # lowercase aliases (rcpaper builders use lowercase)
                              "h1": 60, "h2": 32, "body": 20, "sm": 14}
_C3_X4_SIZES: dict[str, int] = {"H1": 56, "H2": 28, "Body": 18, "Sm": 12,
                                 "h1": 56, "h2": 28, "body": 18, "sm": 12}
_TLSR_SIZES: dict[str, int] = {"H1": 24, "H2": 16, "Body": 12, "Sm": 10,
                                "h1": 24, "h2": 16, "body": 12, "sm": 10}


S3 = DeviceProfile(
    name="s3", width=400, height=600,
    resolve_color=_resolve_s3, sizes=_S3_SIZES,
)
C3_X4 = DeviceProfile(
    name="c3-x4", width=800, height=480,
    resolve_color=_resolve_c3_x4, sizes=_C3_X4_SIZES,
)
TLSR = DeviceProfile(
    name="tlsr", width=250, height=128,
    resolve_color=_resolve_tlsr, sizes=_TLSR_SIZES,
)

PROFILES: dict[str, DeviceProfile] = {S3.name: S3, C3_X4.name: C3_X4, TLSR.name: TLSR}


def get_profile(name: str) -> DeviceProfile:
    if name not in PROFILES:
        raise KeyError(f"unknown device profile {name!r}; known: {sorted(PROFILES)}")
    return PROFILES[name]
```

- [ ] **Step 1.1.2: Write tests for the resolvers**

Path: `producer/tests/test_devices_profile.py`
```python
import pytest
from rcink.devices_profile import S3, C3_X4, TLSR, get_profile


def test_get_profile_known():
    assert get_profile("s3") is S3
    assert get_profile("c3-x4") is C3_X4
    assert get_profile("tlsr") is TLSR


def test_get_profile_unknown_raises():
    with pytest.raises(KeyError):
        get_profile("nope")


@pytest.mark.parametrize("token,expect", [
    ("black", "#000000"), ("white", "#FFFFFF"),
    ("red", "#FF0000"), ("yellow", "#FFFF00"),
    ("green", "#00FF00"), ("blue", "#0000FF"),
    ("accent", "#FF0000"), ("warning", "#FFFF00"),
    ("success", "#00FF00"), ("info", "#0000FF"), ("muted", "#000000"),
])
def test_s3_resolves_full_palette(token, expect):
    assert S3.resolve_color(token) == expect


def test_c3_x4_only_white_stays_white():
    assert C3_X4.resolve_color("white") == "#FFFFFF"
    for t in ("black", "red", "yellow", "green", "blue",
              "accent", "warning", "success", "info", "muted"):
        assert C3_X4.resolve_color(t) == "#000000"


@pytest.mark.parametrize("token,expect", [
    ("black", "#000000"), ("white", "#FFFFFF"),
    ("red", "#FF0000"), ("blue", "#FF0000"),
    ("yellow", "#000000"), ("green", "#000000"),
    ("accent", "#FF0000"), ("warning", "#FF0000"),
    ("success", "#000000"), ("info", "#000000"), ("muted", "#000000"),
])
def test_tlsr_bwr_reduction(token, expect):
    assert TLSR.resolve_color(token) == expect
```

- [ ] **Step 1.1.3: Run tests**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest tests/test_devices_profile.py -v 2>&1 | tail -25
```
Expected: all pass.

- [ ] **Step 1.1.4: Commit**

```bash
cd ~/w/_hw/rcink && git add producer/rcink/devices_profile.py producer/tests/test_devices_profile.py
git commit -m "feat(producer): add rendering-side device profiles (s3, c3-x4, tlsr)"
```

---

## Phase 2 — PIL reference renderer

### Task 2.1: Add Pillow dependency

- [ ] **Step 2.1.1: uv add pillow**

```bash
cd ~/w/_hw/rcink/producer && uv add pillow 2>&1 | tail -5
```
Expected: `pillow` appears in `[dependencies]`.

### Task 2.2: Write the PNG renderer

**Files:**
- Create: `producer/rcink/preview_png.py`

This is the largest file in the plan. ~200 lines. It mirrors the structure of `preview.py` but emits PIL drawing calls instead of HTML.

- [ ] **Step 2.2.1: Write preview_png.py**

Path: `producer/rcink/preview_png.py`

```python
"""PIL-based reference renderer: DSL page → PNG, parameterised by device.

This is the cross-device "spec rendering" — the visual contract every
firmware must match in its host_test. Differences against a firmware's
host_test PNG are bugs in either side and surface via golden diff.

Drawing fidelity is "good enough" not "pixel perfect" — fonts and metrics
mirror each firmware's font registry per the design spec, but text
rasterisation uses PIL's default sans (not the device's bundled font).
What's preserved exactly: layout math, color mapping, primitive geometry,
truncation thresholds. What's not: glyph anti-aliasing details, CJK
shaping. Sufficient to validate layout + color decisions.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from PIL import Image, ImageDraw, ImageFont

from rcink.devices_profile import DeviceProfile


# Defaults; per-device overrides via DeviceProfile.sizes already loaded.
_FONT_CACHE: dict[tuple[int, bool], ImageFont.FreeTypeFont | ImageFont.ImageFont] = {}


def _load_font(px: int, bold: bool) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    """Try to load a system sans font; fall back to PIL's default bitmap."""
    key = (px, bold)
    if key in _FONT_CACHE:
        return _FONT_CACHE[key]
    candidates = (
        # macOS
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFNSDisplay.ttf",
        # Linux (CI)
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    )
    for path in candidates:
        try:
            font = ImageFont.truetype(path, px)
            _FONT_CACHE[key] = font
            return font
        except OSError:
            continue
    font = ImageFont.load_default()
    _FONT_CACHE[key] = font
    return font


@dataclass(slots=True)
class _Rect:
    x: int
    y: int
    w: int
    h: int


def _gap(node: dict[str, Any], default: int = 0) -> int:
    return int(node.get("gap", default))


def _pad(node: dict[str, Any], default: int = 0) -> int:
    return int(node.get("pad", default))


def _measure_text(text: str, font) -> tuple[int, int]:
    # PIL ≥10 uses textbbox; fall back for older
    try:
        l, t, r, b = font.getbbox(text)
        return (r - l, b - t)
    except AttributeError:
        return font.getsize(text)


def _layout_node(node: dict[str, Any], rect: _Rect, prof: DeviceProfile) -> None:
    """Recursively position children. Mirrors the firmware run_layout pass."""
    # Layout pass writes rect mutably onto each node under "_rect".
    # Children of a stack get sliced along the main axis with align on cross.
    node["_rect"] = rect
    typ = node.get("type")
    if typ == "vstack":
        _layout_stack(node, rect, prof, axis="v")
    elif typ == "hstack":
        _layout_stack(node, rect, prof, axis="h")
    else:
        # Leaf nodes don't need recursion; geometry is implicit.
        pass


def _layout_stack(node: dict[str, Any], rect: _Rect, prof: DeviceProfile, axis: str) -> None:
    pad = _pad(node)
    gap = _gap(node)
    align = node.get("align", "start")
    children = node.get("children", []) or []

    # Inner rect after pad
    inner = _Rect(rect.x + pad, rect.y + pad, max(0, rect.w - 2 * pad), max(0, rect.h - 2 * pad))

    # Measure each child's intrinsic size along main axis. Flex spacers fill.
    intrinsic = [_intrinsic_size(c, inner, prof, axis) for c in children]
    main_used = sum(s for s, flex in intrinsic if not flex)
    gaps_used = gap * max(0, len(children) - 1)
    main_total = (inner.h if axis == "v" else inner.w)
    flex_slots = [i for i, (_, fx) in enumerate(intrinsic) if fx]
    flex_pool = max(0, main_total - main_used - gaps_used)
    flex_each = flex_pool // len(flex_slots) if flex_slots else 0

    cursor = inner.y if axis == "v" else inner.x
    for i, child in enumerate(children):
        size, flex = intrinsic[i]
        if flex:
            size = flex_each
        cross_size = inner.w if axis == "v" else inner.h
        cross_origin = inner.x if axis == "v" else inner.y

        if axis == "v":
            child_rect = _Rect(cross_origin, cursor, cross_size, size)
        else:
            child_rect = _Rect(cursor, cross_origin, size, cross_size)
        _layout_node(child, child_rect, prof)
        cursor += size + (gap if i < len(children) - 1 else 0)


def _intrinsic_size(node: dict[str, Any], parent: _Rect, prof: DeviceProfile, axis: str) -> tuple[int, bool]:
    """Return (size_along_axis, is_flex). For unknown node types, return (0, False)."""
    typ = node.get("type")
    if typ == "spacer":
        if "size" in node:
            return (int(node["size"]), False)
        return (0, True)
    if typ == "divider":
        thick = int(node.get("thickness", 1))
        return (thick if axis == "v" else parent.w, False)
    if typ == "text":
        size_token = node.get("size", "Body")
        px = prof.sizes.get(size_token, prof.sizes.get("Body", 14))
        pad = _pad(node)
        return (px + 2 * pad if axis == "v" else parent.w, False)
    if typ == "hbar":
        h = int(node.get("height", 8))
        return (h if axis == "v" else parent.w, False)
    if typ == "sparkline":
        h = int(node.get("height", 32))
        return (h if axis == "v" else parent.w, False)
    if typ == "icon":
        s = int(node.get("size", 16))
        return (s, False)
    if typ == "image":
        h = int(node.get("height", 100))
        return (h if axis == "v" else int(node.get("width", parent.w)), False)
    if typ == "grid":
        h = int(node.get("height", 100))
        return (h if axis == "v" else int(node.get("width", parent.w)), False)
    if typ in ("vstack", "hstack"):
        # Recurse: estimate child sum along axis.
        gap = _gap(node)
        pad = _pad(node)
        children = node.get("children", []) or []
        if not children:
            return (2 * pad, False)
        if (typ == "vstack" and axis == "v") or (typ == "hstack" and axis == "h"):
            total = 2 * pad + gap * max(0, len(children) - 1)
            for c in children:
                cs, cf = _intrinsic_size(c, parent, prof, axis)
                if not cf:
                    total += cs
            return (total, False)
        return (parent.h if axis == "v" else parent.w, False)
    return (0, False)


def _paint_node(node: dict[str, Any], draw: ImageDraw.ImageDraw, prof: DeviceProfile) -> None:
    rect = node.get("_rect")
    if not rect:
        return
    typ = node.get("type")
    if typ in ("vstack", "hstack"):
        # Paint background pad-rect if bg specified (rare on stacks).
        for c in node.get("children", []) or []:
            _paint_node(c, draw, prof)
        return
    if typ == "spacer":
        return
    if typ == "divider":
        color = prof.resolve_color(node.get("color", "black"))
        thick = int(node.get("thickness", 1))
        draw.rectangle((rect.x, rect.y, rect.x + rect.w, rect.y + thick), fill=color)
        return
    if typ == "text":
        _paint_text(node, rect, draw, prof)
        return
    if typ == "hbar":
        _paint_hbar(node, rect, draw, prof)
        return
    if typ == "sparkline":
        _paint_sparkline(node, rect, draw, prof)
        return
    if typ == "icon":
        # No icon assets in reference renderer — draw a placeholder box.
        color = prof.resolve_color(node.get("color", "black"))
        s = int(node.get("size", 16))
        draw.rectangle((rect.x, rect.y, rect.x + s, rect.y + s), outline=color, width=1)
        return
    if typ == "image":
        # No image assets — draw a placeholder box with X.
        color = prof.resolve_color("black")
        draw.rectangle((rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), outline=color, width=1)
        draw.line((rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), fill=color, width=1)
        draw.line((rect.x + rect.w, rect.y, rect.x, rect.y + rect.h), fill=color, width=1)
        return
    if typ == "grid":
        # No values rendering yet — placeholder box.
        color = prof.resolve_color(node.get("color", "black"))
        draw.rectangle((rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), outline=color, width=1)
        return


def _paint_text(node: dict[str, Any], rect: _Rect, draw: ImageDraw.ImageDraw, prof: DeviceProfile) -> None:
    value = str(node.get("value", ""))
    size_token = node.get("size", "Body")
    px = prof.sizes.get(size_token, prof.sizes.get("Body", 14))
    bold = node.get("weight", "Regular").lower() == "bold"
    font = _load_font(px, bold)
    color = prof.resolve_color(node.get("color", "black"))
    bg = node.get("bg")
    pad = _pad(node)

    # bg fill across the full rect.
    if bg:
        draw.rectangle((rect.x, rect.y, rect.x + rect.w, rect.y + rect.h),
                       fill=prof.resolve_color(bg))

    # Truncation with ellipsis if too wide.
    avail_w = rect.w - 2 * pad
    text_w, _ = _measure_text(value, font)
    text = value
    if text_w > avail_w and avail_w > 0:
        while text and _measure_text(text + "…", font)[0] > avail_w:
            text = text[:-1]
        text = text + "…" if text else "…"

    align = (node.get("align") or "Start").lower()
    text_w2, text_h = _measure_text(text, font)
    if align == "center":
        tx = rect.x + (rect.w - text_w2) // 2
    elif align == "end":
        tx = rect.x + rect.w - text_w2 - pad
    else:
        tx = rect.x + pad
    ty = rect.y + (rect.h - text_h) // 2
    draw.text((tx, ty), text, font=font, fill=color)


def _paint_hbar(node: dict[str, Any], rect: _Rect, draw: ImageDraw.ImageDraw, prof: DeviceProfile) -> None:
    color = prof.resolve_color(node.get("color", "black"))
    pct = float(node.get("pct", 0))
    pct = max(0.0, min(100.0, pct))
    # Border
    draw.rectangle((rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), outline=color, width=1)
    # Fill proportional
    fw = int(rect.w * pct / 100.0)
    if fw > 1:
        draw.rectangle((rect.x + 1, rect.y + 1, rect.x + fw, rect.y + rect.h - 1), fill=color)


def _paint_sparkline(node: dict[str, Any], rect: _Rect, draw: ImageDraw.ImageDraw, prof: DeviceProfile) -> None:
    values = node.get("values") or []
    if not values:
        return
    color = prof.resolve_color(node.get("color", "black"))
    vmin = min(values)
    vmax = max(values)
    if vmax == vmin:
        vmax = vmin + 1
    step_x = rect.w / max(1, len(values) - 1)
    points: list[tuple[int, int]] = []
    for i, v in enumerate(values):
        x = rect.x + int(i * step_x)
        norm = (v - vmin) / (vmax - vmin)
        y = rect.y + rect.h - int(norm * rect.h)
        points.append((x, y))
    if len(points) >= 2:
        draw.line(points, fill=color, width=1)


def render_to_png(layout: dict[str, Any], prof: DeviceProfile) -> Image.Image:
    """Render one page layout to a PIL Image at the device's viewport size."""
    img = Image.new("RGB", (prof.width, prof.height), color="#FFFFFF")
    draw = ImageDraw.Draw(img)
    # Work on a copy because layout pass mutates _rect onto nodes.
    import copy
    root = copy.deepcopy(layout)
    _layout_node(root, _Rect(0, 0, prof.width, prof.height), prof)
    _paint_node(root, draw, prof)
    return img
```

- [ ] **Step 2.2.2: Smoke test render**

```bash
cd ~/w/_hw/rcink/producer && uv run python -c "
from rcink.pages.quote import build_quote_page
from rcink.sources.quotes import pick_quote
from rcink.devices_profile import S3, C3_X4, TLSR
from rcink.preview_png import render_to_png
import datetime as dt
quote = pick_quote(dt.date(2026, 5, 24))
page = build_quote_page(quote, now=dt.datetime(2026, 5, 24))
for prof in (S3, C3_X4, TLSR):
    img = render_to_png(page['layout'], prof)
    print(f'{prof.name}: {img.size}')
    img.save(f'/tmp/quote-{prof.name}.png')
print('saved /tmp/quote-{s3,c3-x4,tlsr}.png')
"
```
Expected: prints three image sizes and saves three PNGs. Open them to eyeball.

### Task 2.3: Test the renderer

**Files:**
- Create: `producer/tests/test_preview_png.py`

- [ ] **Step 2.3.1: Write test**

Path: `producer/tests/test_preview_png.py`
```python
"""Smoke tests for the PIL reference renderer."""

import pytest
from rcink.devices_profile import S3, C3_X4, TLSR
from rcink.layout import vstack, hstack, text, hbar, spacer, divider, sparkline
from rcink.preview_png import render_to_png


@pytest.fixture
def kitchen_sink():
    """One layout that exercises every primitive."""
    return vstack(
        pad=8,
        gap=4,
        children=[
            text("HELLO", size="H1", color="accent", weight="bold", align="center"),
            divider(color="black", thickness=2),
            hbar(pct=42, color="accent", height=12, label_left="42%"),
            sparkline([1, 5, 3, 7, 4, 8, 6], color="info", height=24),
            hstack(
                gap=4,
                children=[
                    text("LEFT", size="Body", color="success"),
                    spacer(),
                    text("RIGHT", size="Body", color="warning"),
                ],
            ),
            spacer(size=8),
            text("muted footer", size="Sm", color="muted", align="center"),
        ],
    )


@pytest.mark.parametrize("prof", [S3, C3_X4, TLSR])
def test_renders_kitchen_sink_at_viewport(kitchen_sink, prof):
    img = render_to_png(kitchen_sink, prof)
    assert img.size == (prof.width, prof.height)
    assert img.mode == "RGB"


def test_empty_vstack_renders_blank_canvas():
    img = render_to_png(vstack(children=[]), S3)
    # Should be all white (background).
    px = img.load()
    assert px[10, 10] == (255, 255, 255)


def test_text_renders_some_dark_pixels_on_s3():
    img = render_to_png(
        vstack(pad=10, children=[text("X", size="H1", color="black")]),
        S3,
    )
    # Convert to grayscale; expect at least some dark pixels somewhere.
    g = img.convert("L")
    dark_pixels = sum(1 for p in g.getdata() if p < 64)
    assert dark_pixels > 0, "text should produce some dark pixels"
```

- [ ] **Step 2.3.2: Run**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest tests/test_preview_png.py -v 2>&1 | tail -20
```
Expected: all pass.

- [ ] **Step 2.3.3: Commit**

```bash
cd ~/w/_hw/rcink && git add producer/pyproject.toml producer/rcink/preview_png.py producer/tests/test_preview_png.py
git commit -m "feat(producer): add PIL reference renderer (DSL→PNG, per-device profile)"
```

---

## Phase 3 — Generate goldens

### Task 3.1: Add CLI for golden regeneration

**Files:**
- Create: `producer/rcink/cli_goldens.py`
- Modify: `producer/pyproject.toml` (script entry)

- [ ] **Step 3.1.1: Write cli_goldens.py**

Path: `producer/rcink/cli_goldens.py`

```python
"""Regenerate dsl/golden/<device>/<page>.png from current page builders.

Run with `uv run rcink-goldens` from producer/. Writes/overwrites PNGs
into ../dsl/golden/<device>/.

The page set is hand-listed here (one entry per builder + a fixture
source). For pages that need live data (claude, codex, github, calendar,
todo), we use frozen test fixtures from tests/fixtures/. For deterministic
pages (quote, image), we call the builder directly.
"""
from __future__ import annotations

import datetime as dt
from pathlib import Path
from typing import Callable, Any

import typer

from rcink.devices_profile import C3_X4, S3, TLSR, DeviceProfile
from rcink.preview_png import render_to_png


GOLDEN_ROOT = Path(__file__).resolve().parent.parent.parent / "dsl" / "golden"
DEVICES: tuple[DeviceProfile, ...] = (S3, C3_X4, TLSR)


def _build_quote() -> dict:
    from rcink.pages.quote import build_quote_page
    from rcink.sources.quotes import pick_quote
    q = pick_quote(dt.date(2026, 1, 1))
    return build_quote_page(q, now=dt.datetime(2026, 1, 1, 12, 0))


def _build_image() -> dict:
    from rcink.pages.image import build_image_page
    return build_image_page(
        name="rcink-demo",
        url="https://example.invalid/demo.png",
        width=400,
        height=300,
    )


def _build_claude() -> dict:
    from rcink.pages.claude import build_claude_page
    from rcink.sources.claude import ClaudeUsage
    usage = ClaudeUsage(
        plan="Max 20x",
        session_pct=42.0,
        session_reset_iso="2026-01-01T17:00:00",
        five_hour_pct=60.0,
        five_hour_reset_iso="2026-01-01T17:00:00",
        weekly_pct=15.0,
        weekly_reset_iso="2026-01-07T00:00:00",
        opus_pct=8.0,
        opus_reset_iso="2026-01-07T00:00:00",
        as_of_iso="2026-01-01T12:00:00",
    )
    return build_claude_page(usage, now=dt.datetime(2026, 1, 1, 12, 0))


def _build_codex() -> dict:
    from rcink.pages.codex import build_codex_page
    from rcink.sources.codex import CodexUsage
    usage = CodexUsage(
        plan="Plus",
        five_hour_used_pct=30.0,
        five_hour_reset_iso="2026-01-01T17:00:00",
        weekly_used_pct=18.0,
        weekly_reset_iso="2026-01-07T00:00:00",
        as_of_iso="2026-01-01T12:00:00",
        credits_remaining=None,
        sparkline_24h=None,
    )
    return build_codex_page(usage, now=dt.datetime(2026, 1, 1, 12, 0))


def _build_overview() -> dict:
    """Overview is the most complex page. We build with empty fixtures so it
    degrades gracefully — exercises the layout but not the live-data branches.
    """
    from rcink.pages.overview import build_overview_page
    return build_overview_page(now=dt.datetime(2026, 1, 1, 12, 0))


_BUILDERS: dict[str, Callable[[], dict]] = {
    "quote": _build_quote,
    "image": _build_image,
    "claude": _build_claude,
    "codex": _build_codex,
    "overview": _build_overview,
}


app = typer.Typer(add_completion=False, help="Regenerate per-device golden PNGs.")


@app.command()
def regen(
    pages: list[str] | None = typer.Option(None, "--page", "-p", help="Only these page ids; default = all known"),
    devices: list[str] | None = typer.Option(None, "--device", "-d", help="Only these device profiles; default = all"),
) -> None:
    selected_pages = pages or list(_BUILDERS.keys())
    selected_devices = [
        next(d for d in DEVICES if d.name == name) for name in (devices or [d.name for d in DEVICES])
    ]
    for page in selected_pages:
        builder = _BUILDERS[page]
        result = builder()
        layout = result["layout"] if "layout" in result else result
        for prof in selected_devices:
            out_dir = GOLDEN_ROOT / prof.name
            out_dir.mkdir(parents=True, exist_ok=True)
            out_path = out_dir / f"{page}.png"
            img = render_to_png(layout, prof)
            img.save(out_path)
            typer.echo(f"wrote {out_path.relative_to(GOLDEN_ROOT.parent.parent)}")


def main() -> None:
    app()
```

- [ ] **Step 3.1.2: Register CLI**

Edit `producer/pyproject.toml`, append to `[project.scripts]`:
```toml
rcink-goldens  = "rcink.cli_goldens:main"
```

- [ ] **Step 3.1.3: uv sync**

```bash
cd ~/w/_hw/rcink/producer && uv sync --all-extras 2>&1 | tail -3
```

- [ ] **Step 3.1.4: Regenerate all goldens**

```bash
cd ~/w/_hw/rcink/producer && uv run rcink-goldens regen 2>&1 | tail -25
```
Expected: writes `dsl/golden/{s3,c3-x4,tlsr}/{quote,image,claude,codex,overview}.png` — 15 files. Some pages may use other builder signatures; if any builder call fails, fix the fixture call to match the actual signature (read `producer/rcink/pages/<name>.py` and `producer/rcink/sources/<name>.py` dataclasses).

- [ ] **Step 3.1.5: Eyeball the goldens**

```bash
open ~/w/_hw/rcink/dsl/golden/s3/quote.png
open ~/w/_hw/rcink/dsl/golden/c3-x4/quote.png
open ~/w/_hw/rcink/dsl/golden/tlsr/quote.png
```
Confirm each viewport renders the page sensibly. Look for: text within bounds, colors per-device-profile, no overlap.

- [ ] **Step 3.1.6: Commit golden binaries + CLI**

```bash
cd ~/w/_hw/rcink && git add dsl/golden/ producer/rcink/cli_goldens.py producer/pyproject.toml
git commit -m "feat(goldens): regenerate dsl/golden/<device>/<page>.png across 3 device profiles"
```

### Task 3.2: pytest validates goldens never drift silently

**Files:**
- Create: `producer/tests/test_goldens.py`

This test re-renders each page and asserts its bytes match the on-disk golden. If a producer change shifts the output, this test fails — forcing the developer to either fix the regression or deliberately regenerate via `rcink-goldens regen`.

- [ ] **Step 3.2.1: Write test_goldens.py**

Path: `producer/tests/test_goldens.py`

```python
"""Golden PNG regression test.

Re-renders each (device, page) pair and asserts byte-for-byte match with
dsl/golden/<device>/<page>.png. To update goldens deliberately, run:

    uv run rcink-goldens regen

Then commit the new PNGs alongside the producer change.
"""
from pathlib import Path

import pytest

from rcink.cli_goldens import _BUILDERS
from rcink.devices_profile import C3_X4, S3, TLSR
from rcink.preview_png import render_to_png

GOLDEN_ROOT = Path(__file__).resolve().parent.parent.parent / "dsl" / "golden"
DEVICES = (S3, C3_X4, TLSR)

_pairs = [(prof, page) for prof in DEVICES for page in _BUILDERS]


@pytest.mark.parametrize("prof,page", _pairs, ids=lambda p: p.name if hasattr(p, "name") else p)
def test_golden_matches(prof, page, tmp_path):
    golden = GOLDEN_ROOT / prof.name / f"{page}.png"
    assert golden.exists(), f"missing golden {golden}; run `uv run rcink-goldens regen`"

    layout_dict = _BUILDERS[page]()
    layout = layout_dict["layout"] if "layout" in layout_dict else layout_dict
    img = render_to_png(layout, prof)

    out = tmp_path / "actual.png"
    img.save(out)
    assert out.read_bytes() == golden.read_bytes(), (
        f"golden drift for {prof.name}/{page}.png — "
        "if intentional, run `uv run rcink-goldens regen` and recommit."
    )
```

- [ ] **Step 3.2.2: Run**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest tests/test_goldens.py -v 2>&1 | tail -25
```
Expected: 15 tests pass (3 devices × 5 pages).

- [ ] **Step 3.2.3: Commit**

```bash
cd ~/w/_hw/rcink && git add producer/tests/test_goldens.py
git commit -m "test(goldens): assert each (device, page) PNG matches dsl/golden/ on disk"
```

---

## Phase 4 — Validation & docs

### Task 4.1: All-green check

- [ ] **Step 4.1.1: Full pytest**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest -q 2>&1 | tail -5
```
Expected: 159 (existing) + 15 (goldens) + new palette/render tests = ~195 passed.

- [ ] **Step 4.1.2: git status clean**

```bash
cd ~/w/_hw/rcink && git status
```
Expected: nothing to commit.

### Task 4.2: Document the goldens workflow

**Files:**
- Modify: `dsl/SPEC.md` (append a "Golden testing" section)

- [ ] **Step 4.2.1: Append goldens section to SPEC.md**

At end of `dsl/SPEC.md`, add:

```markdown
## Golden testing

Each (device, page) pair has a checked-in PNG at `dsl/golden/<device>/<page>.png`.
These are the visual contract every firmware's `host_test/` must match.

Workflow:

- **CI / pytest**: `producer/tests/test_goldens.py` re-renders each page via the
  Python reference renderer (`rcink.preview_png`) and asserts byte-equal with
  the on-disk PNG. Any drift fails the test.

- **Deliberate update**: when a producer change is supposed to alter visual
  output, regenerate:

  ```bash
  cd producer && uv run rcink-goldens regen
  ```

  Inspect the resulting PNGs, then commit them alongside the producer change.

- **Per-firmware cross-validation** (future): each firmware's `host_test/`
  shim renders the same page set on the host (cross-compiled with stub I/O)
  and produces PNGs. The CI compares those PNGs against the device's golden.
  Divergence between Python reference and firmware C/C++ = a bug in one side.

The Python reference renderer is "good enough not pixel-perfect" — font
glyph anti-aliasing differs from each firmware's bundled font stack. What it
does enforce strictly: layout math, color-token-to-ink mapping, primitive
geometry, truncation thresholds. Those are what we want golden coverage on.
```

- [ ] **Step 4.2.2: Commit**

```bash
cd ~/w/_hw/rcink && git add dsl/SPEC.md
git commit -m "docs(dsl): document the golden PNG testing workflow"
```

---

## Self-review

Plan checklist against spec §"Testing":

- [x] Python reference renderer producing PNGs across viewports
- [x] Three device profiles with palette resolvers matching firmware tables
- [x] Goldens checked into dsl/golden/
- [x] Test enforcing no-drift
- [x] CLI for deliberate regeneration
- [x] Documentation in dsl/SPEC.md

Out of scope (other plans):
- Per-firmware `host_test/` shims that compare device-rendered PNG against the Python reference. Belongs in the c3-x4 firmware plan and the tlsr host_test extension plan.
- True icon/image asset rendering (current renderer draws placeholder rectangles). Sufficient for layout validation; firmware does the actual asset blit.
- CJK font fidelity. Each firmware bundles its own CJK font; the Python renderer uses system sans. Layouts containing CJK text will not byte-match the firmware host_test exactly — that's expected and the test_goldens test catches drift in the Python renderer's output, not in the firmware's.
