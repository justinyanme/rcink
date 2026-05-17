"""Build a Claude usage page in the magazine style."""

from __future__ import annotations

import datetime as dt

from rcpaper.layout import (
    divider,
    hbar,
    hstack,
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
        gap=3,
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
                    ),
                    hbar(
                        pct=pct if pct is not None else 0,
                        color=color,
                        height=14,
                    ),
                ],
            ),
        ],
    )


def _model_cell(label: str, pct: float) -> dict:
    color = "red" if pct >= 80 else "green"
    return vstack(
        pad=4,
        children=[
            text(label, size="sm", weight="bold"),
            text(f"{int(pct)}%", size="body", color=color),
        ],
    )


def build_claude_page(
    usage: ClaudeUsage,
    *,
    sparkline: list[float] | None,
    updated_at: dt.datetime,
) -> dict:
    children: list[dict] = [
        # Red knockout header
        text(
            "CLAUDE",
            size="h1",
            color="white",
            bg="red",
            pad=8,
            align="center",
        ),
        spacer(size=6),
        # KPI rows
        _kpi_row("5H WINDOW", usage.five_hour_pct, usage.five_hour_reset, "blue"),
        spacer(size=8),
        _kpi_row("7D WINDOW", usage.seven_day_pct, usage.seven_day_reset, "green"),
    ]

    # Per-model row only when we have data
    model_cells = []
    if usage.opus_pct is not None:
        model_cells.append(_model_cell("Opus", usage.opus_pct))
    if usage.sonnet_pct is not None:
        model_cells.append(_model_cell("Sonnet", usage.sonnet_pct))
    if model_cells:
        children.append(spacer(size=10))
        children.append(text("BY MODEL · 7D", size="sm", weight="bold"))
        children.append(hstack(gap=6, children=model_cells))

    # Sparkline only when provided
    if sparkline:
        children.append(spacer(size=10))
        children.append(text("DAILY USAGE · LAST 7", size="sm", weight="bold"))
        children.append(spark(sparkline, color="blue", height=40))

    # Footer
    children.append(spacer())
    children.append(divider(color="black", thickness=1))
    children.append(
        text(
            f"{usage.plan} plan · updated {updated_at:%H:%M}",
            size="sm",
            align="center",
        )
    )

    return vstack(pad=14, gap=4, children=children)
