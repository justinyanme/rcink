"""Build compact KP2 cards from the existing source dataclasses.

Each builder turns one source's rich data into a renderable :class:`Card`:
icon + 8-char title + up to 4 grid rows + the fetch timestamp. Reset rows use
``Row(until=epoch)`` so the keypoint-push job renders a live countdown instead
of the frozen "2h 14m" string the e-ink page builders show.
"""

from __future__ import annotations

import datetime as dt

from rcink.keypoint import LED_HINT_ATTENTION, LED_HINT_NONE, LED_HINT_WARNING, Card, Row
from rcink.sources.claude import ClaudeUsage
from rcink.sources.codex import CodexUsage


def _pct(value: float | None) -> str:
    return f"{int(value or 0)}%"


def _usage_led_hint(*values: float | None) -> int:
    peak = max((float(value or 0) for value in values), default=0.0)
    if peak >= 90.0:
        return LED_HINT_WARNING
    if peak >= 75.0:
        return LED_HINT_ATTENTION
    return LED_HINT_NONE


def claude_card(usage: ClaudeUsage, *, now: dt.datetime) -> Card:
    rows = [Row("5H", _pct(usage.five_hour_pct)), Row("7D", _pct(usage.seven_day_pct))]
    if usage.five_hour_reset_at is not None:
        rows.append(Row("RST", until=usage.five_hour_reset_at))
    rows.append(Row("OP", _pct(usage.opus_pct)))
    return Card(
        id="claude",
        icon="CLAUDE",
        title="CLAUDE",
        led_hint=_usage_led_hint(usage.five_hour_pct, usage.seven_day_pct, usage.opus_pct),
        rows=rows,
        data_ts=int(now.timestamp()),
    )


def codex_card(usage: CodexUsage, *, now: dt.datetime) -> Card:
    rows = [Row("5H", _pct(usage.primary_pct)), Row("7D", _pct(usage.secondary_pct))]
    if usage.primary_reset_at is not None:
        rows.append(Row("RST", until=usage.primary_reset_at))
    return Card(
        id="codex",
        icon="CODEX",
        title="CODEX",
        led_hint=_usage_led_hint(usage.primary_pct, usage.secondary_pct),
        rows=rows,
        data_ts=int(now.timestamp()),
    )
