"""Build a Claude usage page in the magazine style.

Knockout red header, two KPI rows (hbar height 18), optional BY MODEL · 7D
cells, optional USAGE · LAST 24H sparkline. The firmware overlays its own
status bar at the top and nav strip at the bottom, so this builder doesn't
include either.
"""

from __future__ import annotations

from rcpaper.layout import hstack, spacer, text, vstack
from rcpaper.layout import sparkline as spark
from rcpaper.pages._common import kpi_row
from rcpaper.palette import ColorToken
from rcpaper.sources.claude import ClaudeUsage


def _model_cell(label: str, pct: float) -> dict:
    color: ColorToken = "red" if pct >= 80 else "green"
    return vstack(
        pad=4,
        gap=2,
        children=[
            text(label, size="sm", weight="bold"),
            text(
                f"{int(pct)}%",
                size="body",
                color=color,
                weight="bold",
            ),
        ],
    )


def build_claude_page(
    usage: ClaudeUsage,
    *,
    sparkline: list[float] | None,
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
        kpi_row("5H WINDOW", usage.five_hour_pct, usage.five_hour_reset, "blue"),
        spacer(size=10),
        kpi_row("7D WINDOW", usage.seven_day_pct, usage.seven_day_reset, "green"),
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
        children.append(text("USAGE · LAST 24H", size="sm", weight="bold"))
        children.append(spark(sparkline, color="blue", height=56))

    return vstack(pad=14, gap=6, children=children)
