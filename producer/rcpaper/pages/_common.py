"""Shared page-builder helpers."""

from __future__ import annotations

from rcpaper.layout import hbar, hstack, spacer, text, vstack


def kpi_row(label: str, pct: float | None, reset: str, color: str) -> dict:
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
