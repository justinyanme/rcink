"""Tiny DSL: each helper returns a dict matching the rcpapercolor protocol."""

from typing import Any


def _node(t: str, **props: Any) -> dict[str, Any]:
    out: dict[str, Any] = {"type": t}
    for k, v in props.items():
        if v is not None:
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


def divider(*, color: str | None = None, thickness: int | None = None) -> dict:
    return _node("divider", color=color, thickness=thickness)


def text(
    value: str,
    *,
    size: str | None = None,
    color: str | None = None,
    weight: str | None = None,
    align: str | None = None,
    bg: str | None = None,
    pad: int | None = None,
    max_lines: int | None = None,
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
        max_lines=max_lines,
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
        color=color,
        height=height,
        label_left=label_left,
        label_right=label_right,
    )


def icon(name: str, *, size: int | None = None, color: str | None = None) -> dict:
    return _node("icon", name=name, size=size, color=color)


def image(name: str, *, width: int | None = None, height: int | None = None) -> dict:
    return _node("image", name=name, width=width, height=height)


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
        color=color,
        height=height,
        baseline=baseline,
    )


def marker(*, pct: float, color: str | None = None) -> dict:
    return _node("marker", pct=pct, color=color)
