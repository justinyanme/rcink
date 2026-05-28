"""Build a Claude usage page in the magazine style.

Knockout red header, two KPI rows (hbar height 18), optional BY MODEL · 7D
cells, optional USAGE · LAST 24H sparkline. The firmware overlays its own
status bar at the top and nav strip at the bottom, so this builder doesn't
include either.
"""

from __future__ import annotations

from rcink.layout import hbar, hstack, spacer, text, vstack
from rcink.layout import sparkline as spark
from rcink.pages._common import kpi_row
from rcink.palette import ColorToken
from rcink.sources.claude import ClaudeUsage


def build_claude_compact_page(usage: ClaudeUsage) -> dict:
    """Compact CLAUDE page for small panels (e.g. 250x128 BWR TLSR).

    `build_claude_page` targets large displays; on a tiny panel it overflows the
    firmware's fixed layout arena and won't fit visually. Design notes for this
    panel specifically:
      - vstack align=center so the header chip and reset line center (a `text`
        node sizes to its content; the engine centers it against the full-width
        rows).
      - the window label + percent are separate text nodes flanking each bar.
        draw_hbar's built-in labels are drawn white-on-fill with no background,
        so they vanish on the unfilled (white) part of a low bar — external text
        is black-on-white and always legible.
      - colour is BWR: red only signals a near-full window (>=80%); otherwise
        black. `info`/`success` would both collapse to black here.
    """

    def _row(label: str, pct: float | None) -> dict:
        color: ColorToken = "accent" if (pct is not None and pct >= 80) else "black"
        return hstack(
            gap=6,
            align="center",
            children=[
                text(label, size="body", weight="bold"),
                hbar(pct=pct if pct is not None else 0, color=color, height=20),
                text(f"{int(pct)}%" if pct is not None else "—", size="body", weight="bold"),
            ],
        )

    return vstack(
        pad=8,
        gap=8,
        align="center",
        children=[
            text(
                "CLAUDE",
                size="h1",
                color="white",
                bg="accent",
                pad=4,
                align="center",
                weight="bold",
            ),
            _row("5H", usage.five_hour_pct),
            _row("7D", usage.seven_day_pct),
            text(
                f"resets {usage.five_hour_reset}" if usage.five_hour_reset else "",
                size="sm",
                align="center",
            ),
        ],
    )


def _model_cell(label: str, pct: float) -> dict:
    color: ColorToken = "accent" if pct >= 80 else "success"
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
            bg="accent",
            pad=8,
            align="center",
            weight="bold",
        ),
        spacer(size=8),
        kpi_row("5H WINDOW", usage.five_hour_pct, usage.five_hour_reset, "info"),
        spacer(size=10),
        kpi_row("7D WINDOW", usage.seven_day_pct, usage.seven_day_reset, "success"),
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
        children.append(spark(sparkline, color="info", height=56))

    return vstack(pad=14, gap=6, children=children)
