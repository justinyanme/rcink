"""Render a layout dict to an HTML preview matching a device profile.

Not pixel-perfect (browser fonts vs M5GFX/FreeSans), but accurate enough to
iterate on the page builders without flashing the device. Each layout
primitive maps to a `<div>` with flexbox/inline styles.
"""

from __future__ import annotations

import html
from typing import Any

from rcink.devices_profile import S3, DeviceProfile
from rcink.grid_ramp import grid_color_token


def _black(prof: DeviceProfile) -> str:
    return prof.resolve_color("black")


def _white(prof: DeviceProfile) -> str:
    return prof.resolve_color("white")


def _color(token: str | None, prof: DeviceProfile, default: str = "transparent") -> str:
    if token is None:
        return default
    return prof.resolve_color(token)


def _align(token: str | None, axis: str) -> str:
    a = (token or "").lower()
    if axis == "main":
        return {"start": "flex-start", "center": "center", "end": "flex-end"}.get(a, "flex-start")
    return {"start": "flex-start", "center": "center", "end": "flex-end"}.get(a, "flex-start")


def _node_html(node: dict[str, Any], prof: DeviceProfile) -> str:
    t = node.get("type", "")
    pad = int(node.get("pad", 0) or 0)
    gap = int(node.get("gap", 0) or 0)
    black = _black(prof)
    white = _white(prof)

    if t in ("vstack", "hstack"):
        direction = "column" if t == "vstack" else "row"
        # In the C++ engine, `align` sets the cross-axis alignment of children.
        align = _align(node.get("align"), "cross")
        children = "".join(_node_html(c, prof) for c in node.get("children", []) or [])
        style = f"display:flex;flex-direction:{direction};gap:{gap}px;padding:{pad}px;align-items:{align};"
        if t == "vstack":
            # vstacks fill horizontally by default; firmware engine does the same.
            style += "width:100%;box-sizing:border-box;"
        return f'<div class="{t}" style="{style}">{children}</div>'

    if t == "text":
        size = prof.sizes.get(node.get("size", "body"), prof.sizes["body"])
        weight = "700" if node.get("weight") == "bold" else "400"
        color = _color(node.get("color"), prof, black)
        bg = _color(node.get("bg"), prof)
        dy = int(node.get("dy", 0) or 0)
        align_text = {"center": "center", "end": "right"}.get(node.get("align"), "left")
        value = html.escape(str(node.get("value", "")))
        style = (
            f"font-size:{size}px;line-height:1.15;font-weight:{weight};color:{color};"
            f"background:{bg};padding:{pad}px;text-align:{align_text};"
            f"position:relative;top:{dy}px;"
        )
        # Centered/right-aligned text expects the box to fill its parent main axis.
        if align_text != "left":
            style += "width:100%;box-sizing:border-box;"
        return f'<div class="text" style="{style}">{value}</div>'

    if t == "hbar":
        pct = max(0.0, min(100.0, float(node.get("pct", 0) or 0)))
        color = _color(node.get("color"), prof, black)
        height = int(node.get("height", 14) or 14)
        label_l = html.escape(str(node.get("label_left", "")))
        label_r = html.escape(str(node.get("label_right", "")))
        # Border 1px, 2px inset for the fill — mirrors `draw_hbar` in
        # src/render/primitives.cpp.
        return (
            f'<div class="hbar" style="position:relative;flex:1;'
            f'height:{height}px;border:1px solid {black};box-sizing:border-box;">'
            f'<div style="position:absolute;left:2px;top:2px;bottom:2px;'
            f'width:calc({pct}% - 4px);background:{color};"></div>'
            f'<div style="position:absolute;left:6px;top:0;bottom:0;'
            f'color:{white};font-weight:700;font-size:{prof.sizes["sm"]}px;display:flex;align-items:center;">{label_l}</div>'
            f'<div style="position:absolute;right:6px;top:0;bottom:0;'
            f'color:{black};font-weight:700;font-size:{prof.sizes["sm"]}px;display:flex;align-items:center;">{label_r}</div>'
            f"</div>"
        )

    if t == "divider":
        color = _color(node.get("color"), prof, black)
        th = int(node.get("thickness", 1) or 1)
        return f'<div class="divider" style="width:100%;height:{th}px;background:{color};"></div>'

    if t == "spacer":
        size = node.get("size")
        if size:
            s = int(size)
            return f'<div class="spacer-fixed" style="min-width:{s}px;min-height:{s}px;"></div>'
        # Flexible spacer pushes siblings apart.
        return '<div class="spacer-flex" style="flex:1;"></div>'

    if t == "sparkline":
        values = node.get("values") or []
        color = _color(node.get("color"), prof, black)
        if not values:
            return '<div class="sparkline-empty" style="height:32px;"></div>'
        lo, hi = min(values), max(values)
        if hi == lo:
            lo, hi = lo - 1, hi + 1
        width, height = 200, 32
        pts = []
        n = len(values)
        for i, v in enumerate(values):
            x = (i * (width - 1)) / max(1, (n - 1))
            y = height - ((v - lo) / (hi - lo)) * (height - 2) - 1
            pts.append(f"{x:.1f},{y:.1f}")
        polyline = " ".join(pts)
        return (
            f'<svg class="sparkline" viewBox="0 0 {width} {height}" '
            f'preserveAspectRatio="none" style="width:100%;height:{height}px;">'
            f'<polyline fill="none" stroke="{color}" stroke-width="2" points="{polyline}" />'
            f'<line x1="0" y1="{height - 1}" x2="{width}" y2="{height - 1}" stroke="{black}" />'
            f"</svg>"
        )

    if t == "icon":
        color = _color(node.get("color"), prof, black)
        s = int(node.get("size", 16) or 16)
        return f'<div class="icon" style="width:{s}px;height:{s}px;background:{color};"></div>'

    if t == "image":
        width = int(node.get("width", 240) or 240)
        height = int(node.get("height", 180) or 180)
        url = node.get("url")
        if url:
            safe_url = html.escape(str(url), quote=True)
            return (
                f'<div class="image" style="width:{width}px;height:{height}px;'
                f'border:1px solid {black};box-sizing:border-box;overflow:hidden;">'
                f'<img src="{safe_url}" style="width:100%;height:100%;object-fit:contain;display:block;" /></div>'
            )
        name = html.escape(str(node.get("name", "")))
        return (
            f'<div class="image" style="width:{width}px;height:{height}px;'
            f"border:1px solid {black};padding:4px;"
            f'font-size:{prof.sizes["sm"]}px;box-sizing:border-box;">img:{name}</div>'
        )

    if t == "marker":
        # Overlay; in browser preview just show a vertical bar at the requested pct.
        pct = max(0.0, min(100.0, float(node.get("pct", 0) or 0)))
        color = _color(node.get("color"), prof, black)
        return (
            f'<div class="marker" style="position:relative;height:1px;">'
            f'<div style="position:absolute;left:calc({pct}% - 1px);'
            f'top:-6px;width:2px;height:14px;background:{color};"></div>'
            f"</div>"
        )

    if t == "grid":
        cols = int(node.get("cols", 0) or 0)
        rows = int(node.get("rows", 0) or 0)
        gap = int(node.get("gap", 2) or 0)
        width = int(node.get("width", 0) or 0) or "100%"
        height = int(node.get("height", 60) or 60)
        values = node.get("values") or []

        def cell(value: float | int | None) -> str:
            return _color(grid_color_token(value), prof)

        cells = []
        for col in range(cols):
            for row in range(rows):
                idx = col * rows + row
                value = values[idx] if idx < len(values) else 0
                cells.append(f'<div style="background:{cell(value)};border:1px solid {black};"></div>')
        style = (
            f"display:grid;grid-template-columns:repeat({cols},1fr);"
            f"grid-template-rows:repeat({rows},1fr);gap:{gap}px;"
            f"width:{width if isinstance(width, str) else f'{width}px'};height:{height}px;"
        )
        return f'<div class="grid" style="{style}">{"".join(cells)}</div>'

    return f"<!-- unknown node type: {html.escape(t)} -->"


def to_html(layout: dict[str, Any], *, title: str = "rcpaper preview", prof: DeviceProfile = S3) -> str:
    body = _node_html(layout, prof)
    black = _black(prof)
    white = _white(prof)
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{html.escape(title)}</title>
<style>
  :root {{ color-scheme: light; }}
  body {{
    margin: 0; padding: 24px;
    background: {white};
    font-family: -apple-system, "Helvetica Neue", Arial, sans-serif;
  }}
  .frame {{
    width: {prof.width}px; height: {prof.height}px;
    background: {white};
    border: 1px solid {black};
    margin: 0 auto;
    overflow: hidden;
  }}
  .label {{
    width: {prof.width}px; margin: 0 auto 8px;
    font-size: 12px; color: {black};
  }}
</style>
</head>
<body>
  <div class="label">{html.escape(title)} — {prof.name} {prof.width}×{prof.height}, browser approximation</div>
  <div class="frame">{body}</div>
</body>
</html>"""
