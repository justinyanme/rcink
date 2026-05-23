"""macOS Calendar source ported from rcpapercolor."""

import datetime as dt
import sys
from dataclasses import dataclass
from typing import Any


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


def _nsdate_to_dt(value: Any) -> dt.datetime:
    return dt.datetime.fromtimestamp(
        value.timeIntervalSince1970(),
        tz=dt.UTC,
    ).astimezone()


def fetch_todays_events(now: dt.datetime | None = None) -> TodaysCalendar:
    if sys.platform != "darwin":
        raise RuntimeError("Calendar source requires macOS EventKit")

    import EventKit as ek
    import Foundation as foundation

    now = now or dt.datetime.now().astimezone()
    store = ek.EKEventStore.alloc().init()

    calendar = foundation.NSCalendar.currentCalendar()
    units = (
        foundation.NSCalendarUnitYear
        | foundation.NSCalendarUnitMonth
        | foundation.NSCalendarUnitDay
    )
    today_components = calendar.components_fromDate_(units, foundation.NSDate.date())
    midnight = calendar.dateFromComponents_(today_components)
    end_of_day = midnight.dateByAddingTimeInterval_(86400)

    calendars = store.calendarsForEntityType_(ek.EKEntityTypeEvent)
    predicate = store.predicateForEventsWithStartDate_endDate_calendars_(
        midnight,
        end_of_day,
        calendars,
    )
    raw_events = store.eventsMatchingPredicate_(predicate) or []

    events: list[CalEvent] = []
    for event in raw_events:
        title = (event.title() or "").strip()
        if not title:
            continue
        calendar_name = event.calendar().title() if event.calendar() else ""
        if calendar_name.startswith("Weather for"):
            continue
        events.append(
            CalEvent(
                start=_nsdate_to_dt(event.startDate()),
                end=_nsdate_to_dt(event.endDate()),
                title=title,
                calendar=calendar_name,
                is_all_day=bool(event.isAllDay()),
            )
        )
    events.sort(key=lambda item: item.start)
    return TodaysCalendar(today=now.date(), events=events)
