"""Compact 250x128 Codex usage page."""

from ..layout import hbar, text, vstack
from ..sources.codex import CodexUsage, fetch_codex_usage


def _pct(value: float | None) -> int:
    if value is None:
        return 0
    return max(0, min(100, int(round(value))))


def _pct_text(value: float | None) -> str:
    return "--%" if value is None else f"{_pct(value)}%"


def _window_label(minutes: int | None) -> str:
    if minutes is None:
        return "WINDOW"
    if minutes <= 60:
        return f"{minutes}M"
    if minutes <= 24 * 60:
        return f"{minutes // 60}H"
    return f"{minutes // (24 * 60)}D"


def build_page(usage: CodexUsage | None = None) -> dict:
    usage = usage or fetch_codex_usage()
    primary = _pct(usage.primary_pct)
    secondary = ""
    if usage.secondary_pct is not None:
        secondary = (
            f"{_window_label(usage.secondary_window_min)} {_pct_text(usage.secondary_pct)} "
            f"reset {usage.secondary_reset or '--'}"
        )
    elif usage.credits_dollars is not None:
        secondary = f"CREDITS ${usage.credits_dollars:.2f}"
    else:
        secondary = usage.plan

    return vstack(
        pad=4,
        gap=3,
        children=[
            text(
                "CODEX",
                size="h1",
                color="white",
                bg="red",
                pad=4,
                align="center",
                weight="bold",
            ),
            hbar(
                pct=primary,
                color="red" if primary >= 80 else "black",
                height=24,
                label_left=_window_label(usage.primary_window_min),
                label_right=_pct_text(usage.primary_pct),
            ),
            text(f"reset {usage.primary_reset or '--'}", size="sm", weight="bold"),
            text(secondary, size="sm"),
        ],
    )
