"""Tiny DSL: each helper returns a dict matching the rcpapercolor protocol."""

from __future__ import annotations

from typing import Any

from rcink.palette import ColorToken, normalize_color


def _node(t: str, **props: Any) -> dict[str, Any]:
    out: dict[str, Any] = {"type": t}
    for k, v in props.items():
        if v is not None:
            if k in {"color", "bg"}:
                v = normalize_color(v, field=k)
            out[k] = v
    return out


def vstack(*, gap=None, pad=None, align=None, children: list[dict] | None = None) -> dict:
    return _node(
        "vstack",
        gap=gap,
        pad=pad,
        align=align,
        children=children if children is not None else [],
    )


def hstack(*, gap=None, pad=None, align=None, children: list[dict] | None = None) -> dict:
    return _node(
        "hstack",
        gap=gap,
        pad=pad,
        align=align,
        children=children if children is not None else [],
    )


def spacer(*, size: int | None = None) -> dict:
    return _node("spacer", size=size)


def divider(*, color: ColorToken | None = None, thickness: int | None = None) -> dict:
    return _node("divider", color=color, thickness=thickness)


def text(
    value: str,
    *,
    size: str | None = None,
    color: ColorToken | None = None,
    weight: str | None = None,
    align: str | None = None,
    bg: ColorToken | None = None,
    pad: int | None = None,
    dy: int | None = None,
) -> dict:
    return _node(
        "text",
        value=value,
        size=size,
        color=color,
        weight=weight,
        align=align,
        bg=bg,
        pad=pad,
        dy=dy,
    )


def hbar(
    *,
    pct: float,
    color: ColorToken | None = None,
    height: int | None = None,
    label_left: str | None = None,
    label_right: str | None = None,
) -> dict:
    return _node(
        "hbar",
        pct=pct,
        color=color,
        height=height,
        label_left=label_left,
        label_right=label_right,
    )


def icon(name: str, *, size: int | None = None, color: ColorToken | None = None) -> dict:
    return _node("icon", name=name, size=size, color=color)


def image(
    name: str | None = None,
    *,
    url: str | None = None,
    format: str | None = None,
    width: int | None = None,
    height: int | None = None,
) -> dict:
    if bool(name) == bool(url):
        raise ValueError("image() requires exactly one of name=... or url=...")
    return _node("image", name=name, url=url, format=format, width=width, height=height)


def sparkline(
    values: list[float],
    *,
    color: ColorToken | None = None,
    height: int | None = None,
    baseline: str | int | None = None,
) -> dict:
    return _node(
        "sparkline",
        values=list(values),
        color=color,
        height=height,
        baseline=baseline,
    )


def marker(*, pct: float, color: ColorToken | None = None) -> dict:
    return _node("marker", pct=pct, color=color)


def grid(
    *,
    cols: int,
    rows: int,
    values: list[int | float | None],
    color: ColorToken | None = None,
    gap: int | None = None,
    width: int | None = None,
    height: int | None = None,
) -> dict:
    return _node(
        "grid",
        cols=cols,
        rows=rows,
        gap=gap,
        color=color,
        values=list(values),
        width=width,
        height=height,
    )
