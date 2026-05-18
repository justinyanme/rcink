"""Build the Calendar page — today's events in editorial style.

Mirrors `buildCalendar` from the handoff: TODAY masthead, NEXT hero (red
sticker + time + title + duration), an UP NEXT list with colored time
ticks per calendar source, and a footer brand row. If today has no events,
the page degrades to a friendly empty state.
"""

from __future__ import annotations

import datetime as dt

from rcpaper.layout import divider, hbar, hstack, icon, spacer, text, vstack
from rcpaper.sources.calendar import CalEvent, TodaysCalendar

# Cycle of "ink" colors for event ticks in the UP NEXT list. Per-calendar
# coloring (mapping EKCalendar.color → palette) is overkill for a 7-color
# E6 panel; cycling through saturated swatches keeps the list readable and
# matches the editorial layout the handoff drew.
_TICK_COLORS = ("blue", "red", "green", "yellow")


def _fmt_time(when: dt.datetime, all_day: bool) -> str:
    if all_day:
        return "ALL"
    return when.strftime("%H:%M")


def _fmt_duration(start: dt.datetime, end: dt.datetime, all_day: bool) -> str:
    if all_day:
        return "all day"
    mins = max(1, int((end - start).total_seconds() // 60))
    if mins < 60:
        return f"{mins}m"
    if mins % 60 == 0:
        return f"{mins // 60}h"
    return f"{mins // 60}h {mins % 60}m"


def _next_event(events: list[CalEvent], now: dt.datetime) -> CalEvent | None:
    # Prefer the next event that has not yet ended; fall back to the first all-day.
    upcoming = [e for e in events if e.end > now and not e.is_all_day]
    if upcoming:
        return upcoming[0]
    all_day = [e for e in events if e.is_all_day]
    return all_day[0] if all_day else None


def _next_in_label(when: dt.datetime, now: dt.datetime) -> str:
    delta_min = int((when - now).total_seconds() // 60)
    if delta_min <= 0:
        return "now"
    if delta_min < 60:
        return f"in {delta_min}m"
    h, m = divmod(delta_min, 60)
    return f"in {h}h" if m == 0 else f"in {h}h {m}m"


def build_calendar_page(cal: TodaysCalendar, *, now: dt.datetime) -> dict:
    today_label = cal.today.strftime("%a · %b %d").upper()

    children: list[dict] = [
        hstack(
            align="center",
            children=[
                text("TODAY", size="sm", weight="bold"),
                spacer(),
                text(today_label, size="sm", weight="bold"),
            ],
        ),
        spacer(size=2),
        divider(color="black", thickness=2),
        spacer(size=8),
    ]

    if not cal.events:
        children += [
            spacer(size=20),
            text("NO EVENTS", size="h1", color="green", weight="bold", align="center"),
            spacer(size=8),
            text("clear runway", size="body", align="center"),
        ]
    else:
        nxt = _next_event(cal.events, now)
        if nxt is not None:
            children += [
                text("NEXT", size="sm", weight="bold", color="white", bg="red", pad=4),
                spacer(size=4),
                hstack(
                    align="end",
                    gap=6,
                    children=[
                        text(_fmt_time(nxt.start, nxt.is_all_day), size="h1", color="red", weight="bold"),
                        vstack(
                            gap=2,
                            children=[
                                text(nxt.title, size="body", weight="bold", max_lines=1),
                                text(
                                    f"{_fmt_duration(nxt.start, nxt.end, nxt.is_all_day)} · "
                                    f"{_next_in_label(nxt.start, now)}",
                                    size="sm",
                                ),
                            ],
                        ),
                    ],
                ),
                spacer(size=10),
                divider(color="black", thickness=1),
                spacer(size=6),
            ]

        # UP NEXT list of remaining events (skipping the hero one).
        rest = [e for e in cal.events if e is not nxt]
        if rest:
            children.append(text("UP NEXT", size="sm", weight="bold"))
            children.append(spacer(size=2))
            for i, ev in enumerate(rest[:6]):  # cap at 6 to keep the page tidy
                tick = _TICK_COLORS[i % len(_TICK_COLORS)]
                children.append(
                    hstack(
                        gap=8,
                        align="center",
                        children=[
                            text(_fmt_time(ev.start, ev.is_all_day), size="body", weight="bold", color=tick),
                            hbar(pct=100, color=tick, height=4),
                            text(ev.title, size="sm", weight="bold", max_lines=1),
                        ],
                    )
                )

    children += [
        spacer(),
        divider(color="black", thickness=2),
        spacer(size=4),
        hstack(
            align="center",
            gap=6,
            children=[
                icon("calendar", size=14, color="blue"),
                text(f"CALENDAR · {len(cal.events)} TODAY", size="sm", weight="bold"),
            ],
        ),
    ]

    return vstack(pad=18, gap=6, children=children)
