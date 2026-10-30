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

import copy
from dataclasses import dataclass
from typing import Any

from PIL import Image, ImageDraw, ImageFont

from rcink.devices_profile import DeviceProfile
from rcink.grid_ramp import grid_color_token

_FONT_CACHE: dict[tuple[int, bool], Any] = {}


def _load_font(px: int, bold: bool) -> Any:
    """Try a system sans; fall back to PIL's default bitmap."""
    key = (px, bold)
    if key in _FONT_CACHE:
        return _FONT_CACHE[key]
    candidates = (
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFNSDisplay.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
        if bold
        else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
        if bold
        else "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
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


def _measure_text(text: str, font: Any) -> tuple[int, int]:
    try:
        left, top, right, bottom = font.getbbox(text)
        return (right - left, bottom - top)
    except AttributeError:
        return font.getsize(text)


def _layout_node(node: dict[str, Any], rect: _Rect, prof: DeviceProfile) -> None:
    node["_rect"] = rect
    typ = node.get("type")
    if typ == "vstack":
        _layout_stack(node, rect, prof, axis="v")
    elif typ == "hstack":
        _layout_stack(node, rect, prof, axis="h")


def _layout_stack(node: dict[str, Any], rect: _Rect, prof: DeviceProfile, axis: str) -> None:
    pad = _pad(node)
    gap = _gap(node)
    children = node.get("children", []) or []

    inner = _Rect(rect.x + pad, rect.y + pad, max(0, rect.w - 2 * pad), max(0, rect.h - 2 * pad))

    intrinsic = [_intrinsic_size(c, inner, prof, axis) for c in children]
    main_used = sum(s for s, flex in intrinsic if not flex)
    gaps_used = gap * max(0, len(children) - 1)
    main_total = inner.h if axis == "v" else inner.w
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

        child_rect = (
            _Rect(cross_origin, cursor, cross_size, size)
            if axis == "v"
            else _Rect(cursor, cross_origin, size, cross_size)
        )
        _layout_node(child, child_rect, prof)
        cursor += size + (gap if i < len(children) - 1 else 0)


def _intrinsic_size(node: dict[str, Any], parent: _Rect, prof: DeviceProfile, axis: str) -> tuple[int, bool]:
    """Return (size_along_axis, is_flex)."""
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
        if axis == "v":
            return (px + 2 * pad, False)
        bold = (node.get("weight") or "Regular").lower() == "bold"
        font = _load_font(px, bold)
        text_w, _ = _measure_text(str(node.get("value", "")), font)
        return (min(parent.w, text_w + 2 * pad), False)
    if typ == "hbar":
        h = int(node.get("height", 8))
        if axis == "v":
            return (h, False)
        if "width" in node:
            return (int(node["width"]), False)
        return (0, True)
    if typ == "marker":
        return (4 if axis == "v" else 1, False)
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
        gap = _gap(node)
        pad = _pad(node)
        children = node.get("children", []) or []
        if not children:
            return (2 * pad, False)
        is_main = (typ == "vstack" and axis == "v") or (typ == "hstack" and axis == "h")
        if is_main:
            total = 2 * pad + gap * max(0, len(children) - 1)
            for c in children:
                cs, cf = _intrinsic_size(c, parent, prof, axis)
                if not cf:
                    total += cs
            return (total, False)
        # cross-axis: stack height/width is max of children's cross extents + 2*pad
        max_cross = 0
        for c in children:
            cs, _cf = _intrinsic_size(c, parent, prof, axis)
            if cs > max_cross:
                max_cross = cs
        return (max_cross + 2 * pad, False)
    return (0, False)


def _paint_node(node: dict[str, Any], draw: ImageDraw.ImageDraw, prof: DeviceProfile) -> None:
    rect = node.get("_rect")
    if not rect:
        return
    typ = node.get("type")
    if typ in ("vstack", "hstack"):
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
    if typ == "marker":
        _paint_marker(node, rect, draw, prof)
        return
    if typ == "sparkline":
        _paint_sparkline(node, rect, draw, prof)
        return
    if typ == "icon":
        color = prof.resolve_color(node.get("color", "black"))
        s = int(node.get("size", 16))
        draw.rectangle((rect.x, rect.y, rect.x + s, rect.y + s), outline=color, width=1)
        return
    if typ == "image":
        color = prof.resolve_color("black")
        draw.rectangle((rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), outline=color, width=1)
        draw.line((rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), fill=color, width=1)
        draw.line((rect.x + rect.w, rect.y, rect.x, rect.y + rect.h), fill=color, width=1)
        return
    if typ == "grid":
        _paint_grid(node, rect, draw, prof)
        return


def _paint_text(node: dict[str, Any], rect: _Rect, draw: ImageDraw.ImageDraw, prof: DeviceProfile) -> None:
    value = str(node.get("value", ""))
    size_token = node.get("size", "Body")
    px = prof.sizes.get(size_token, prof.sizes.get("Body", 14))
    bold = (node.get("weight") or "Regular").lower() == "bold"
    font = _load_font(px, bold)
    color = prof.resolve_color(node.get("color", "black"))
    bg = node.get("bg")
    pad = _pad(node)

    if bg:
        draw.rectangle(
            (rect.x, rect.y, rect.x + rect.w, rect.y + rect.h),
            fill=prof.resolve_color(bg),
        )

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
    ty = rect.y + (rect.h - text_h) // 2 + int(node.get("dy", 0) or 0)
    draw.text((tx, ty), text, font=font, fill=color)


def _paint_hbar(node: dict[str, Any], rect: _Rect, draw: ImageDraw.ImageDraw, prof: DeviceProfile) -> None:
    color = prof.resolve_color(node.get("color", "black"))
    pct = float(node.get("pct", 0))
    pct = max(0.0, min(100.0, pct))
    draw.rectangle((rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), outline=color, width=1)
    fw = int(rect.w * pct / 100.0)
    if fw > 1:
        draw.rectangle(
            (rect.x + 1, rect.y + 1, rect.x + fw, rect.y + rect.h - 1),
            fill=color,
        )
    font = _load_font(prof.sizes.get("Sm", 10), bold=True)
    label_left = str(node.get("label_left", ""))
    if label_left:
        _, text_h = _measure_text(label_left, font)
        draw.text((rect.x + 4, rect.y + (rect.h - text_h) // 2), label_left, font=font, fill="#FFFFFF")
    label_right = str(node.get("label_right", ""))
    if label_right:
        text_w, text_h = _measure_text(label_right, font)
        draw.text(
            (rect.x + rect.w - text_w - 4, rect.y + (rect.h - text_h) // 2),
            label_right,
            font=font,
            fill="#000000",
        )


def _paint_marker(node: dict[str, Any], rect: _Rect, draw: ImageDraw.ImageDraw, prof: DeviceProfile) -> None:
    pct = max(0.0, min(100.0, float(node.get("pct", 0))))
    color = prof.resolve_color(node.get("color", "red"))
    x = rect.x + int(rect.w * pct / 100.0)
    draw.line((x, rect.y - 4, x, rect.y + rect.h + 4), fill=color, width=1)


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


def _grid_color(value: float | int | None, prof: DeviceProfile) -> str:
    return prof.resolve_color(grid_color_token(value))


def _paint_grid(node: dict[str, Any], rect: _Rect, draw: ImageDraw.ImageDraw, prof: DeviceProfile) -> None:
    cols = int(node.get("cols", 0) or 0)
    rows = int(node.get("rows", 0) or 0)
    if cols <= 0 or rows <= 0:
        return
    gap = int(node.get("gap", 2) or 0)
    avail_w = rect.w - (cols - 1) * gap
    avail_h = rect.h - (rows - 1) * gap
    if avail_w <= 0 or avail_h <= 0:
        return
    cell = min(avail_w // cols, avail_h // rows)
    if cell <= 0:
        return
    total_w = cols * cell + (cols - 1) * gap
    total_h = rows * cell + (rows - 1) * gap
    ox = rect.x + (rect.w - total_w) // 2
    oy = rect.y + (rect.h - total_h) // 2
    values = node.get("values") or []
    border = prof.resolve_color("black")
    for col in range(cols):
        for row in range(rows):
            idx = col * rows + row
            value = values[idx] if idx < len(values) else 0
            x = ox + col * (cell + gap)
            y = oy + row * (cell + gap)
            draw.rectangle((x, y, x + cell, y + cell), fill=_grid_color(value, prof), outline=border, width=1)


def unsupported_node_type(layout: dict[str, Any], prof: DeviceProfile) -> str | None:
    typ = str(layout.get("type", ""))
    if typ in prof.unsupported:
        return typ
    for child in layout.get("children", []) or []:
        if unsupported := unsupported_node_type(child, prof):
            return unsupported
    return None


def render_to_png(layout: dict[str, Any], prof: DeviceProfile) -> Image.Image:
    """Render one page layout to a PIL Image at the device's viewport size."""
    if unsupported := unsupported_node_type(layout, prof):
        raise ValueError(f"{prof.name} does not support DSL node type {unsupported!r}")
    img = Image.new("RGB", (prof.width, prof.height), color="#FFFFFF")
    draw = ImageDraw.Draw(img)
    root = copy.deepcopy(layout)
    _layout_node(root, _Rect(0, 0, prof.width, prof.height), prof)
    _paint_node(root, draw, prof)
    return img
