"""macOS Calendar source — reads today's events via EventKit.

Requires the rcpaper-producer process to have Full Disk / Calendar access; the
first run triggers the system permission prompt. EventKit is macOS-only;
importing this module on other platforms fails fast.
"""

from __future__ import annotations

import datetime as dt
import sys
from dataclasses import dataclass

if sys.platform != "darwin":  # pragma: no cover - module is mac-only
    raise RuntimeError("rcpaper.sources.calendar requires macOS (EventKit)")

import EventKit as ek
import Foundation as F


@dataclass(slots=True)
class CalEvent:
    start: dt.datetime
    end: dt.datetime
    title: str
    calendar: str
    is_all_day: bool


@dataclass(slots=True)
class TodaysCalendar:
    today: dt.date
    events: list[CalEvent]


def _nsdate_to_dt(d: F.NSDate) -> dt.datetime:
    # NSDate.timeIntervalSince1970 is a UTC epoch in seconds. Convert to a
    # local-time-aware datetime so the page can format hh:mm in the user's tz.
    return dt.datetime.fromtimestamp(d.timeIntervalSince1970(), tz=dt.timezone.utc).astimezone()


def fetch_todays_events(now: dt.datetime | None = None) -> TodaysCalendar:
    now = now or dt.datetime.now().astimezone()
    store = ek.EKEventStore.alloc().init()

    cal_obj = F.NSCalendar.currentCalendar()
    units = F.NSCalendarUnitYear | F.NSCalendarUnitMonth | F.NSCalendarUnitDay
    today_comps = cal_obj.components_fromDate_(units, F.NSDate.date())
    midnight = cal_obj.dateFromComponents_(today_comps)
    end_of_day = midnight.dateByAddingTimeInterval_(86400)

    cals = store.calendarsForEntityType_(ek.EKEntityTypeEvent)
    pred = store.predicateForEventsWithStartDate_endDate_calendars_(midnight, end_of_day, cals)
    ns_events = store.eventsMatchingPredicate_(pred) or []

    out: list[CalEvent] = []
    for ev in ns_events:
        title = (ev.title() or "").strip()
        if not title:
            continue
        cal_name = ev.calendar().title() if ev.calendar() else ""
        # Skip the auto-injected weather "Weather for $city" entries — they're
        # rendered as events but they're not user-scheduled and crowd the list.
        if cal_name.startswith("Weather for"):
            continue
        out.append(
            CalEvent(
                start=_nsdate_to_dt(ev.startDate()),
                end=_nsdate_to_dt(ev.endDate()),
                title=title,
                calendar=cal_name,
                is_all_day=bool(ev.isAllDay()),
            )
        )
    out.sort(key=lambda e: e.start)
    return TodaysCalendar(today=now.date(), events=out)
