"""Tiny layout DSL — dict-builders matching the on-the-wire JSON schema.

Mirrors rcpapercolor/producer/rcpaper/layout.py; adds palette validation.
"""

from typing import Any

from .palette import validate_color


def _node(t: str, **props: Any) -> dict[str, Any]:
    out: dict[str, Any] = {"type": t}
    for k, v in props.items():
        if v is not None:
            out[k] = v
    return out


def vstack(
    *,
    gap: int | None = None,
    pad: int | None = None,
    align: str | None = None,
    children: list[dict] | None = None,
) -> dict:
    return _node(
        "vstack",
        gap=gap,
        pad=pad,
        align=align,
        children=children if children is not None else [],
    )


def hstack(
    *,
    gap: int | None = None,
    pad: int | None = None,
    align: str | None = None,
    children: list[dict] | None = None,
) -> dict:
    return _node(
        "hstack",
        gap=gap,
        pad=pad,
        align=align,
        children=children if children is not None else [],
    )


def spacer(*, size: int | None = None) -> dict:
    return _node("spacer", size=size)


def divider(*, color: str | None = None, thickness: int | None = None) -> dict:
    return _node(
        "divider",
        color=validate_color(color, allow_none=True),
        thickness=thickness,
    )


def text(
    value: str,
    *,
    size: str | None = None,
    color: str | None = None,
    weight: str | None = None,
    align: str | None = None,
    bg: str | None = None,
    pad: int | None = None,
) -> dict:
    return _node(
        "text",
        value=value,
        size=size,
        color=validate_color(color, allow_none=True),
        weight=weight,
        align=align,
        bg=validate_color(bg, allow_none=True),
        pad=pad,
    )


def hbar(
    *,
    pct: float,
    color: str | None = None,
    height: int | None = None,
    label_left: str | None = None,
    label_right: str | None = None,
) -> dict:
    return _node(
        "hbar",
        pct=pct,
        color=validate_color(color, allow_none=True),
        height=height,
        label_left=label_left,
        label_right=label_right,
    )


def icon(name: str, *, size: int | None = None, color: str | None = None) -> dict:
    return _node(
        "icon",
        name=name,
        size=size,
        color=validate_color(color, allow_none=True),
    )


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
    return _node(
        "image",
        name=name,
        url=url,
        format=format,
        width=width,
        height=height,
    )


def sparkline(
    values: list[float],
    *,
    color: str | None = None,
    height: int | None = None,
    baseline: str | int | None = None,
) -> dict:
    return _node(
        "sparkline",
        values=list(values),
        color=validate_color(color, allow_none=True),
        height=height,
        baseline=baseline,
    )


def marker(*, pct: float, color: str | None = None) -> dict:
    return _node(
        "marker",
        pct=pct,
        color=validate_color(color, allow_none=True),
    )
