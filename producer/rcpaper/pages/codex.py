"""Build a Codex usage page in the magazine style."""

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
from rcpaper.sources.codex import CodexUsage


def _window_label(minutes: int | None) -> str:
    if minutes is None:
        return "WINDOW"
    if minutes <= 60:
        return f"{minutes}M WINDOW"
    if minutes <= 24 * 60:
        return f"{minutes // 60}H WINDOW"
    if minutes == 7 * 24 * 60:
        return "WEEKLY"
    return f"{minutes // (24 * 60)}D WINDOW"


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


def build_codex_page(
    usage: CodexUsage,
    *,
    sparkline: list[float] | None,
    updated_at: dt.datetime,
) -> dict:
    children: list[dict] = [
        text(
            "CODEX",
            size="h1",
            color="white",
            bg="green",
            pad=8,
            align="center",
        ),
        spacer(size=6),
        _kpi_row(
            _window_label(usage.primary_window_min),
            usage.primary_pct,
            usage.primary_reset,
            "blue",
        ),
    ]

    if usage.secondary_pct is not None:
        children.append(spacer(size=8))
        children.append(
            _kpi_row(
                _window_label(usage.secondary_window_min),
                usage.secondary_pct,
                usage.secondary_reset,
                "blue",
            )
        )

    if sparkline:
        children.append(spacer(size=10))
        children.append(text("DAILY USAGE · LAST 7", size="sm", weight="bold"))
        children.append(spark(sparkline, color="blue", height=40))

    if usage.credits_dollars is not None:
        children.append(spacer(size=10))
        children.append(
            text(
                f"CREDITS  ${usage.credits_dollars:.2f}",
                size="body",
                weight="bold",
                color="black",
                bg="yellow",
                pad=6,
                align="center",
            )
        )

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
