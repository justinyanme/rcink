"""Build the Quote-of-the-day page in editorial poster style.

Masthead row, body lines in h1 with alternating ink colors
(black/blue/black/red), attribution flush-right at the bottom.
"""

from __future__ import annotations

import datetime as dt

from rcink.layout import divider, hstack, icon, spacer, text, vstack
from rcink.palette import ColorToken
from rcink.sources.quotes import Quote

# Color cycle for body lines. Index = line position.
_LINE_COLORS: tuple[ColorToken | None, ...] = (None, "info", None, "accent")


def build_quote_page(quote: Quote, *, now: dt.datetime) -> dict:
    masthead = hstack(
        align="start",
        children=[
            text("QUOTE", size="sm", weight="bold"),
            spacer(),
            text(now.strftime("%a · %b %d").upper(), size="sm"),
        ],
    )

    # Pick the largest font size whose longest line still fits the viewport
    # width (540 px - 2*24 pad = 492 px usable). Approximate avg-bold-char
    # widths in M5GFX FreeSansBold: h1≈22, h2≈16, body≈11 px. The renderer
    # also truncates with an ellipsis as a safety net, but downgrading the
    # font keeps every word visible — far better for a poster page.
    longest = max((len(line) for line in quote.lines[:4]), default=0)
    if longest <= 22:
        line_size = "h1"
    elif longest <= 30:
        line_size = "h2"
    else:
        line_size = "body"

    body: list[dict] = [
        text('"', size=line_size, color="accent", weight="bold"),
    ]
    for i, line in enumerate(quote.lines[:4]):
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
