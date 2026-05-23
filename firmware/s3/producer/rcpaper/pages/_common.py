"""Shared page-builder helpers."""

from __future__ import annotations

import datetime as dt
import unicodedata

from rcpaper.layout import hbar, hstack, spacer, text, vstack
from rcpaper.palette import ColorToken


def kpi_row(label: str, pct: float | None, reset: str, color: ColorToken) -> dict:
    """Two-row block: [LABEL ... resets …] / [N% bar].

    Used identically by the Claude and Codex pages — header on top, value on
    bottom, hbar takes the remaining width. `pct=None` renders as an em dash.
    """
    return vstack(
        gap=2,
        children=[
            hstack(
                align="center",
                children=[
                    text(label, size="sm", weight="bold"),
                    spacer(),
                    text(f"resets {reset}" if reset else "", size="sm"),
                ],
            ),
            hstack(
                gap=8,
                align="center",
                children=[
                    text(
                        f"{int(pct)}%" if pct is not None else "—",
                        size="h1",
                        color=color,
                        weight="bold",
                    ),
                    hbar(pct=pct if pct is not None else 0, color=color, height=18),
                ],
            ),
        ],
    )


def cell_width(c: str) -> int:
    """1 for narrow / 2 for wide. Matches the VT323-vs-efontCN width ratio."""
    return 2 if unicodedata.east_asian_width(c) in ("W", "F") else 1


def visual_width(s: str) -> int:
    return sum(cell_width(c) for c in s)


def _trim(s: str, *, limit: int) -> str:
    """Truncate `s` to ≤ `limit` visual cells, appending `…` if cut."""
    if visual_width(s) <= limit:
        return s
    out = ""
    width = 0
    for c in s:
        w = cell_width(c)
        if width + w + 1 > limit:
            break
        out += c
        width += w
    return out.rstrip() + "…"


def wrap_visual(body: str, *, line_limit: int, max_lines: int) -> list[str]:
    """Greedy word-wrap by visual cells; CJK-aware.

    Breaks on whitespace when possible, falls back to hard character break for
    spaceless runs (CJK). Trailing overflow on the final line is truncated with
    `…`. Returns ≤ `max_lines` non-empty lines.
    """
    if max_lines <= 0:
        return []
    if visual_width(body) <= line_limit:
        return [body]

    lines: list[str] = []
    remaining = body
    while remaining and len(lines) < max_lines:
        if visual_width(remaining) <= line_limit:
            lines.append(remaining)
            break
        # If this is the last line we're allowed to emit, trim with ellipsis.
        if len(lines) == max_lines - 1:
            lines.append(_trim(remaining, limit=line_limit))
            return lines
        # Walk chars accumulating width; remember the last whitespace inside
        # the budget. Prefer breaking there.
        width = 0
        last_space = -1
        hard_break = len(remaining)
        for i, c in enumerate(remaining):
            w = cell_width(c)
            if width + w > line_limit:
                # If the overflowing character is whitespace, breaking here
                # (remaining[:i]) keeps everything before it on the line.
                if c.isspace():
                    last_space = i
                hard_break = i
                break
            width += w
            if c.isspace():
                last_space = i
        cut = last_space if last_space > 0 else hard_break
        lines.append(remaining[:cut].rstrip())
        remaining = remaining[cut:].lstrip()
    return lines


def relative_short(when: dt.datetime | None, *, now: dt.datetime) -> str:
    """Compact age label: '5m', '3h', '2d', '1w', '4mo'. Empty string for None."""
    if when is None:
        return ""
    delta = now - when
    seconds = max(0, int(delta.total_seconds()))
    if seconds < 60:
        return "now"
    minutes = seconds // 60
    if minutes < 60:
        return f"{minutes}m"
    hours = minutes // 60
    if hours < 24:
        return f"{hours}h"
    days = hours // 24
    if days < 7:
        return f"{days}d"
    weeks = days // 7
    if weeks < 5:
        return f"{weeks}w"
    months = max(1, days // 30)
    return f"{months}mo"
