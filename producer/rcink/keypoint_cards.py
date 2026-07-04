"""Build compact KP3 cards from the existing source dataclasses.

Each builder turns one source's rich data into a renderable :class:`Card`:
icon + 8-char title + up to 4 grid rows + the fetch timestamp. Reset rows use
``Row(until=epoch)`` so the keypoint-push job renders a live countdown instead
of the frozen "2h 14m" string the e-ink page builders show.
"""

from __future__ import annotations

import datetime as dt
from typing import TYPE_CHECKING

from rcink.keypoint import LED_HINT_ACTIVE, LED_HINT_ATTENTION, LED_HINT_NONE, LED_HINT_WARNING, Card, Row
from rcink.sources.claude import ClaudeUsage
from rcink.sources.codex import CodexUsage
from rcink.sources.weather import Weather

if TYPE_CHECKING:
    from rcink.sources.calendar import CalEvent, TodaysCalendar
    from rcink.sources.reminders import TodaysReminders


def _pct(value: float | None) -> str:
    if value is None:
        raise ValueError("usage pct is required")
    return f"{int(value)}%"


def _usage_led_hint(*values: float | None) -> int:
    if any(value is None for value in values):
        raise ValueError("usage pct is required")
    peak = max((float(value) for value in values), default=0.0)
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


def _bar(value: float | None) -> int:
    if value is None:
        raise ValueError("usage pct is required")
    return int(value)


def _fit_ascii(value: str, *, limit: int) -> str:
    cleaned = "".join(ch if ch != "|" and 0x20 <= ord(ch) <= 0x7E else "?" for ch in value.strip())
    cleaned = " ".join(cleaned.split())
    return (cleaned or "-")[:limit]


def _small_count(value: int) -> str:
    return "999+" if value > 999 else str(value)


def _weather_icon(code: int) -> str:
    if code in {0, 1}:
        return "SUN"
    if code in {2, 3, 45, 48}:
        return "CLOUD"
    if 51 <= code <= 67 or 80 <= code <= 82 or 95 <= code <= 99:
        return "RAIN"
    return "TEMP"


def _weather_led_hint(weather: Weather) -> int:
    if 95 <= weather.code <= 99:
        return LED_HINT_WARNING
    if weather.precip_probability is not None and weather.precip_probability >= 60:
        return LED_HINT_ATTENTION
    if 51 <= weather.code <= 67 or 80 <= weather.code <= 82:
        return LED_HINT_ATTENTION
    return LED_HINT_NONE


def _next_calendar_event(calendar: TodaysCalendar, *, now: dt.datetime) -> CalEvent | None:
    for event in calendar.events:
        if event.is_all_day or event.end > now:
            return event
    return calendar.upcoming[0] if calendar.upcoming else None


def claude_card(usage: ClaudeUsage, *, now: dt.datetime) -> Card:
    rows: list[Row] = []
    if usage.five_hour_reset_at is not None:
        rows.append(Row("5H", until=usage.five_hour_reset_at))
    else:
        rows.append(Row("5H", _pct(usage.five_hour_pct)))
    rows.append(Row(bar=_bar(usage.five_hour_pct)))
    if usage.seven_day_reset_at is not None:
        rows.append(Row("7D", until=usage.seven_day_reset_at))
    else:
        rows.append(Row("7D", _pct(usage.seven_day_pct)))
    rows.append(Row(bar=_bar(usage.seven_day_pct)))
    return Card(
        id="claude",
        icon="CLAUDE",
        title="CLAUDE",
        led_hint=_usage_led_hint(usage.five_hour_pct, usage.seven_day_pct),
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
    rows.append(Row(bar=_bar(usage.primary_pct)))
    if usage.secondary_reset_at is not None:
        rows.append(Row(secondary_win, until=usage.secondary_reset_at))
    else:
        rows.append(Row(secondary_win, _pct(usage.secondary_pct)))
    rows.append(Row(bar=_bar(usage.secondary_pct)))
    return Card(
        id="codex",
        icon="CODEX",
        title="CODEX",
        led_hint=_usage_led_hint(usage.primary_pct, usage.secondary_pct),
        rows=rows,
        data_ts=int(now.timestamp()),
        show_timestamp=False,
    )


def weather_card(weather: Weather, *, now: dt.datetime) -> Card:
    rows = [
        Row("T", f"{round(weather.temp)}{weather.unit}"),
        Row("H", f"{round(weather.high)}{weather.unit}"),
        Row("L", f"{round(weather.low)}{weather.unit}"),
    ]
    if weather.precip_probability is not None:
        rows.append(Row("POP", f"{weather.precip_probability}%"))
    return Card(
        id="weather",
        icon=_weather_icon(weather.code),
        title="WEATHER",
        led_hint=_weather_led_hint(weather),
        rows=rows,
        data_ts=int(weather.observed_at.timestamp()),
    )


def calendar_card(calendar: TodaysCalendar, *, now: dt.datetime) -> Card:
    event = _next_calendar_event(calendar, now=now)
    if event is None:
        rows = [Row("TOD", "0"), Row("NXT", "NONE")]
        led_hint = LED_HINT_NONE
    elif event.is_all_day:
        rows = [
            Row("ALL", "DAY"),
            Row("EV", _fit_ascii(event.title, limit=5)),
            Row("CNT", _small_count(len(calendar.events))),
        ]
        led_hint = LED_HINT_ACTIVE
    else:
        start_ts = int(event.start.timestamp())
        rows = [
            Row("AT", event.start.strftime("%H:%M")),
            Row("IN", until=start_ts),
            Row("EV", _fit_ascii(event.title, limit=5)),
            Row("CNT", _small_count(len(calendar.events))),
        ]
        minutes = (event.start - now).total_seconds() / 60
        led_hint = LED_HINT_ATTENTION if 0 <= minutes <= 30 else LED_HINT_ACTIVE
    return Card(
        id="calendar",
        icon="TIME",
        title="CAL",
        led_hint=led_hint,
        rows=rows,
        data_ts=int(now.timestamp()),
    )


def todo_card(reminders: TodaysReminders, *, now: dt.datetime) -> Card:
    count = reminders.incomplete_total
    led_hint = LED_HINT_WARNING if count >= 20 else LED_HINT_ATTENTION if count >= 5 else LED_HINT_NONE
    rows = [Row("CNT", _small_count(count))]
    if reminders.items:
        top = reminders.items[0]
        rows.append(Row("TOP", _fit_ascii(top.title, limit=4)))
        if top.due is not None:
            due_ts = int(top.due.timestamp())
            rows.append(Row("D", until=due_ts) if due_ts > int(now.timestamp()) else Row("DUE", "NOW"))
        elif top.priority > 0:
            rows.append(Row("PRI", str(top.priority)))
        else:
            rows.append(Row("LST", _fit_ascii(reminders.list_name, limit=4)))
    else:
        rows.append(Row("OK", "YES"))
        rows.append(Row("LST", _fit_ascii(reminders.list_name, limit=4)))
    return Card(
        id="todo",
        icon="WARN" if led_hint >= LED_HINT_ATTENTION else "NONE",
        title="TODO",
        led_hint=led_hint,
        rows=rows,
        data_ts=int(now.timestamp()),
    )
