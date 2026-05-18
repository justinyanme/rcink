"""Build a Claude usage page in the magazine style.

Mirrors `buildClaudeMagazine` from the design canvas: knockout red header,
KPI rows with hbars (height 18), BY MODEL · 7D, DAILY USAGE · LAST 7 sparkline,
hairline + footer row. The firmware overlays the page-rotation indicator at
the bottom 26 px, so this builder doesn't include prev/next/index — only the
plan + last-updated metadata footer.
"""

from __future__ import annotations

import datetime as dt

from rcpaper.layout import (
    divider,
    hbar,
    hstack,
    icon,
    spacer,
    text,
    vstack,
)
from rcpaper.layout import (
    sparkline as spark,
)
from rcpaper.sources.claude import ClaudeUsage


def _kpi_row(label: str, pct: float | None, reset: str, color: str) -> dict:
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


def _model_cell(label: str, pct: float) -> dict:
    return vstack(
        pad=4,
        gap=2,
        children=[
            text(label, size="sm", weight="bold"),
            text(
                f"{int(pct)}%",
                size="body",
                color="red" if pct >= 80 else "green",
                weight="bold",
            ),
        ],
    )


def build_claude_page(
    usage: ClaudeUsage,
    *,
    sparkline: list[float] | None,
    updated_at: dt.datetime,
) -> dict:
    children: list[dict] = [
        text(
            "CLAUDE",
            size="h1",
            color="white",
            bg="red",
            pad=8,
            align="center",
            weight="bold",
        ),
        spacer(size=8),
        _kpi_row("5H WINDOW", usage.five_hour_pct, usage.five_hour_reset, "blue"),
        spacer(size=10),
        _kpi_row("7D WINDOW", usage.seven_day_pct, usage.seven_day_reset, "green"),
    ]

    model_cells = []
    if usage.opus_pct is not None:
        model_cells.append(_model_cell("OPUS", usage.opus_pct))
    if usage.sonnet_pct is not None:
        model_cells.append(_model_cell("SONNET", usage.sonnet_pct))
    if model_cells:
        children.append(spacer(size=12))
        children.append(text("BY MODEL · 7D", size="sm", weight="bold"))
        children.append(hstack(gap=6, children=model_cells))

    if sparkline:
        children.append(spacer(size=10))
        children.append(text("DAILY USAGE · LAST 7", size="sm", weight="bold"))
        children.append(spark(sparkline, color="blue", height=56))

    # Flex spacer pushes the footer to the bottom of whatever room is left after
    # the firmware indicator strip is subtracted from the page viewport.
    children.append(spacer())
    children.append(divider(color="black", thickness=1))
    children.append(
        hstack(
            align="center",
            gap=6,
            children=[
                icon("claude", size=12, color="red"),
                text(
                    f"{usage.plan} · updated {updated_at:%H:%M}",
                    size="sm",
                ),
            ],
        ),
    )

    return vstack(pad=14, gap=6, children=children)
