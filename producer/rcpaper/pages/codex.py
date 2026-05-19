"""Build a Codex usage page in the magazine style.

Mirrors `buildCodexMagazine` from the design canvas: knockout green header,
two KPI rows (hbar height 18), DAILY USAGE · LAST 7 sparkline, yellow CREDITS
sticker, hairline + metadata footer. The firmware paints the page-rotation
indicator over the bottom 26 px.
"""

from __future__ import annotations

from rcpaper.layout import spacer, text, vstack
from rcpaper.layout import sparkline as spark
from rcpaper.pages._common import kpi_row
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


def build_codex_page(
    usage: CodexUsage,
    *,
    sparkline: list[float] | None,
) -> dict:
    children: list[dict] = [
        text(
            "CODEX",
            size="h1",
            color="white",
            bg="green",
            pad=8,
            align="center",
            weight="bold",
        ),
        spacer(size=8),
        kpi_row(
            _window_label(usage.primary_window_min),
            usage.primary_pct,
            usage.primary_reset,
            "blue",
        ),
    ]

    if usage.secondary_pct is not None:
        children.append(spacer(size=12))
        children.append(
            kpi_row(
                _window_label(usage.secondary_window_min),
                usage.secondary_pct,
                usage.secondary_reset,
                "blue",
            )
        )

    if sparkline:
        children.append(spacer(size=12))
        children.append(text("USAGE · LAST 24H", size="sm", weight="bold"))
        children.append(spark(sparkline, color="blue", height=56))

    if usage.credits_dollars is not None:
        children.append(spacer(size=12))
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

    return vstack(pad=14, gap=6, children=children)
