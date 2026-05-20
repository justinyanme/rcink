"""macOS Reminders source ported from rcpapercolor."""

import datetime as dt
import sys
import threading
import time
from dataclasses import dataclass
from typing import Any


@dataclass(slots=True)
class Reminder:
    title: str
    due: dt.datetime | None
    updated: dt.datetime | None
    priority: int


@dataclass(slots=True)
class TodaysReminders:
    list_name: str
    items: list[Reminder]
    incomplete_total: int


def _due_to_dt(components: Any, foundation: Any) -> dt.datetime | None:
    if components is None:
        return None
    date = foundation.NSCalendar.currentCalendar().dateFromComponents_(components)
    if date is None:
        return None
    return dt.datetime.fromtimestamp(
        date.timeIntervalSince1970(),
        tz=dt.UTC,
    ).astimezone()


def _nsdate_to_dt(value: Any) -> dt.datetime | None:
    if value is None:
        return None
    return dt.datetime.fromtimestamp(
        value.timeIntervalSince1970(),
        tz=dt.UTC,
    ).astimezone()


def _fetch_sync(store: Any, predicate: Any, foundation: Any) -> list[Any]:
    done = threading.Event()
    output: list[Any] = []

    def callback(reminders: Any) -> None:
        if reminders:
            output.extend(list(reminders))
        done.set()

    store.fetchRemindersMatchingPredicate_completion_(predicate, callback)
    run_loop = foundation.NSRunLoop.currentRunLoop()
    start = time.time()
    while not done.is_set() and time.time() - start < 5.0:
        run_loop.runUntilDate_(foundation.NSDate.dateWithTimeIntervalSinceNow_(0.05))
    if not done.is_set():
        raise TimeoutError("EventKit reminders fetch timed out")
    return output


def fetch_todays_reminders(now: dt.datetime | None = None) -> TodaysReminders:
    if sys.platform != "darwin":
        raise RuntimeError("Reminders source requires macOS EventKit")

    import EventKit as ek
    import Foundation as foundation

    now = now or dt.datetime.now().astimezone()
    store = ek.EKEventStore.alloc().init()
    default_calendar = store.defaultCalendarForNewReminders()
    list_name = default_calendar.title() if default_calendar else "Reminders"
    calendars = [default_calendar] if default_calendar else None

    predicate = store.predicateForIncompleteRemindersWithDueDateStarting_ending_calendars_(
        None,
        None,
        calendars,
    )
    raw = _fetch_sync(store, predicate, foundation)
    end_of_day = dt.datetime.combine(
        now.date() + dt.timedelta(days=1),
        dt.time.min,
        tzinfo=now.tzinfo,
    )

    items: list[Reminder] = []
    for reminder in raw:
        title = (reminder.title() or "").strip()
        if not title:
            continue
        due = _due_to_dt(reminder.dueDateComponents(), foundation)
        if due is None or due < end_of_day:
            items.append(
                Reminder(
                    title=title,
                    due=due,
                    updated=_nsdate_to_dt(reminder.lastModifiedDate())
                    or _nsdate_to_dt(reminder.creationDate()),
                    priority=int(reminder.priority()),
                )
            )

    def sort_key(item: Reminder) -> tuple[float, int, float, str]:
        priority = item.priority if item.priority > 0 else 10
        updated = item.updated.timestamp() if item.updated is not None else float("-inf")
        due = item.due.timestamp() if item.due is not None else float("inf")
        return (-updated, priority, due, item.title.casefold())

    items.sort(key=sort_key)
    return TodaysReminders(
        list_name=list_name,
        items=items,
        incomplete_total=len(raw),
    )
