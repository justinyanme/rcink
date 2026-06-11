"""Build compact KP3 cards from the existing source dataclasses.

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


def _window_label(minutes: int | None) -> str:
    if minutes is None:
        return "?"
    if minutes % (24 * 60) == 0:
        return f"{minutes // (24 * 60)}D"
    if minutes % 60 == 0:
        return f"{minutes // 60}H"
    return "?"


def claude_card(usage: ClaudeUsage, *, now: dt.datetime) -> Card:
    rows: list[Row] = []
    if usage.five_hour_reset_at is not None:
        rows.append(Row("5H", until=usage.five_hour_reset_at))
    else:
        rows.append(Row("5H", _pct(usage.five_hour_pct)))
    rows.append(Row(bar=int(usage.five_hour_pct or 0)))
    if usage.seven_day_reset_at is not None:
        rows.append(Row("7D", until=usage.seven_day_reset_at))
    else:
        rows.append(Row("7D", _pct(usage.seven_day_pct)))
    rows.append(Row(bar=int(usage.seven_day_pct or 0)))
    return Card(
        id="claude",
        icon="CLAUDE",
        title="CLAUDE",
        led_hint=_usage_led_hint(usage.five_hour_pct, usage.seven_day_pct, usage.opus_pct),
        rows=rows,
        data_ts=int(now.timestamp()),
        show_timestamp=False,
    )


def codex_card(usage: CodexUsage, *, now: dt.datetime) -> Card:
    primary_win = _window_label(usage.primary_window_min)
    secondary_win = _window_label(usage.secondary_window_min)
    rows: list[Row] = []
    if usage.primary_reset_at is not None:
        rows.append(Row(primary_win, until=usage.primary_reset_at))
    else:
        rows.append(Row(primary_win, _pct(usage.primary_pct)))
    rows.append(Row(bar=int(usage.primary_pct or 0)))
    if usage.secondary_reset_at is not None:
        rows.append(Row(secondary_win, until=usage.secondary_reset_at))
    else:
        rows.append(Row(secondary_win, _pct(usage.secondary_pct)))
    rows.append(Row(bar=int(usage.secondary_pct or 0)))
    return Card(
        id="codex",
        icon="CODEX",
        title="CODEX",
        led_hint=_usage_led_hint(usage.primary_pct, usage.secondary_pct),
        rows=rows,
        data_ts=int(now.timestamp()),
        show_timestamp=False,
    )
