"""Compact 250x128 Calendar page."""

import datetime as dt

from ..layout import hbar, text, vstack
from ..sources.calendar import CalEvent, TodaysCalendar, fetch_todays_events


def _fmt_time(event: CalEvent) -> str:
    if event.is_all_day:
        return "ALL"
    return event.start.strftime("%H:%M")


def _fmt_duration(event: CalEvent) -> str:
    if event.is_all_day:
        return "all day"
    minutes = max(1, int((event.end - event.start).total_seconds() // 60))
    if minutes < 60:
        return f"{minutes}m"
    hours, rest = divmod(minutes, 60)
    return f"{hours}h" if rest == 0 else f"{hours}h {rest}m"


def _trim(value: str, limit: int) -> str:
    return value if len(value) <= limit else value[: limit - 3].rstrip() + "..."


def _next_event(events: list[CalEvent], now: dt.datetime) -> CalEvent | None:
    for event in events:
        if not event.is_all_day and event.end > now:
            return event
    return next((event for event in events if event.is_all_day), None)


def _day_progress(now: dt.datetime) -> int:
    seconds = now.hour * 3600 + now.minute * 60 + now.second
    return int(seconds / 86400 * 100)


def build_page(
    calendar: TodaysCalendar | None = None,
    *,
    now: dt.datetime | None = None,
) -> dict:
    now = now or dt.datetime.now().astimezone()
    calendar = calendar or fetch_todays_events(now=now)
    date_label = calendar.today.strftime("%a %m-%d").upper()
    next_event = _next_event(calendar.events, now)
    remaining = [event for event in calendar.events if event is not next_event]

    if next_event is None:
        lines = [
            text("NO EVENTS", size="body", weight="bold", align="center"),
            text("clear runway", size="sm", align="center"),
        ]
    else:
        lines = [
            text(
                f"{_fmt_time(next_event)} {_trim(next_event.title, 24)}",
                size="body",
                weight="bold",
            ),
            text(f"{_fmt_duration(next_event)} - {next_event.calendar}", size="sm"),
        ]
        if remaining:
            first = remaining[0]
            lines.append(text(f"NEXT {_fmt_time(first)} {_trim(first.title, 22)}", size="sm"))

    return vstack(
        pad=4,
        gap=3,
        children=[
            text(
                date_label,
                size="h1",
                color="white",
                bg="red",
                pad=4,
                align="center",
                weight="bold",
            ),
            hbar(
                pct=_day_progress(now),
                color="black",
                height=14,
                label_left="TODAY",
                label_right=f"{len(calendar.events)} ev",
            ),
            *lines,
        ],
    )
