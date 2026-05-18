"""Build the Quote-of-the-day page in editorial poster style.

Matches `buildQuote` from the design canvas: masthead row, body lines in h1 with
alternating ink colors (black/blue/black/red), attribution flush-right at the
bottom. Within protocol — no proposed primitives needed.
"""

from __future__ import annotations

import datetime as dt

from rcpaper.layout import divider, hstack, icon, spacer, text, vstack
from rcpaper.sources.quotes import Quote

# Color cycle for body lines. Index = line position. Picked to match the
# design canvas's editorial direction.
_LINE_COLORS: tuple[str | None, ...] = (None, "blue", None, "red")


def build_quote_page(quote: Quote, *, now: dt.datetime) -> dict:
    masthead = hstack(
        align="start",
        children=[
            text("QUOTE", size="sm", weight="bold"),
            spacer(),
            text(now.strftime("%a · %b %d").upper(), size="sm"),
        ],
    )

    body: list[dict] = [
        text('"', size="h1", color="red", weight="bold"),
    ]
    for i, line in enumerate(quote.lines[:4]):
        body.append(
            text(line, size="h1", weight="bold", color=_LINE_COLORS[i % len(_LINE_COLORS)]),
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
                icon("quote", size=14, color="red"),
                text("QUOTE OF THE DAY", size="sm", weight="bold"),
            ],
        ),
    ]

    return vstack(pad=24, gap=0, children=children)
