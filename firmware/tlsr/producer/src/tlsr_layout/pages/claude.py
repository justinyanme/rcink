"""Compact 250x128 Claude usage page."""

from ..layout import hbar, spacer, text, vstack
from ..sources.claude import ClaudeUsage, fetch_claude_usage


def _pct(value: float | None) -> int:
    if value is None:
        return 0
    return max(0, min(100, int(round(value))))


def _pct_text(value: float | None) -> str:
    return "--%" if value is None else f"{_pct(value)}%"


def build_page(usage: ClaudeUsage | None = None) -> dict:
    usage = usage or fetch_claude_usage()
    five_hour = _pct(usage.five_hour_pct)
    model_parts = []
    if usage.opus_pct is not None:
        model_parts.append(f"OPUS {_pct_text(usage.opus_pct)}")
    if usage.sonnet_pct is not None:
        model_parts.append(f"SONNET {_pct_text(usage.sonnet_pct)}")

    return vstack(
        pad=4,
        gap=3,
        children=[
            text(
                "CLAUDE",
                size="h1",
                color="white",
                bg="red",
                pad=4,
                align="center",
                weight="bold",
            ),
            hbar(
                pct=five_hour,
                color="red" if five_hour >= 80 else "black",
                height=24,
                label_left="5H",
                label_right=_pct_text(usage.five_hour_pct),
            ),
            text(
                f"5H reset {usage.five_hour_reset or '--'}",
                size="sm",
                weight="bold",
            ),
            text(
                f"7D {_pct_text(usage.seven_day_pct)} reset {usage.seven_day_reset or '--'}",
                size="sm",
            ),
            spacer(size=2),
            text(" / ".join(model_parts) or usage.plan, size="sm"),
        ],
    )
