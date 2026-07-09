"""Build the Quote-of-the-day page in editorial poster style.

Masthead row, body lines in h1 with alternating ink colors
(black/blue/black/red), attribution flush-right at the bottom.
"""

from __future__ import annotations

import datetime as dt

from rcink.layout import divider, hstack, icon, spacer, text, vstack
from rcink.pages._common import wrap_visual_all
from rcink.palette import ColorToken
from rcink.sources.quotes import Quote

# Color cycle for body lines. Index = line position.
_LINE_COLORS: tuple[ColorToken | None, ...] = (None, "info", None, "accent")
_QUOTE_FITS: tuple[tuple[str, int, int], ...] = (
    # size, visual cells per line, max body lines
    ("h1", 22, 4),
    ("h2", 30, 6),
    ("body", 44, 8),
    ("sm", 64, 10),
)


def _fit_quote_lines(quote: Quote) -> tuple[str, list[str]]:
    source_lines = [line.strip() for line in quote.lines if line.strip()]
    body = " ".join(source_lines)
    if not body:
        raise ValueError("quote must contain at least one non-empty line")
    for size, line_limit, max_lines in _QUOTE_FITS:
        if len(source_lines) <= max_lines and all(
            len(wrap_visual_all(line, line_limit=line_limit)) == 1 for line in source_lines
        ):
            return size, source_lines
        lines = wrap_visual_all(body, line_limit=line_limit)
        if len(lines) <= max_lines:
            return size, lines
    raise ValueError(f"quote is too long to fit: {body!r}")


def build_quote_page(quote: Quote, *, now: dt.datetime) -> dict:
    masthead = hstack(
        align="start",
        children=[
            text("QUOTE", size="sm", weight="bold"),
            spacer(),
            text(now.strftime("%a · %b %d").upper(), size="sm"),
        ],
    )

    line_size, lines = _fit_quote_lines(quote)

    body: list[dict] = [
        text('"', size=line_size, color="accent", weight="bold"),
    ]
    for i, line in enumerate(lines):
        body.append(
            text(line, size=line_size, weight="bold", color=_LINE_COLORS[i % len(_LINE_COLORS)]),
        )

    children: list[dict] = [
        masthead,
        spacer(size=8),
        divider(color="black", thickness=2),
        spacer(size=20),
        *body,
        spacer(size=20),
        divider(color="black", thickness=1),
        spacer(size=8),
        hstack(
            align="end",
            children=[
                spacer(),
                text(quote.attrib, size="body", weight="bold"),
            ],
        ),
        spacer(),
        divider(color="black", thickness=2),
        spacer(size=4),
        hstack(
            align="center",
            gap=6,
            children=[
                icon("quote", size=16, color="accent"),
                text("QUOTE OF THE DAY", size="sm", weight="bold"),
            ],
        ),
    ]

    return vstack(pad=24, gap=0, children=children)
