"""macOS Reminders source — reads incomplete reminders via EventKit.

We scope to the default reminders list (System Settings → Reminders → Default
List) so multi-list users don't see hundreds of items. The fetch API is
asynchronous, so we pump NSRunLoop until the completion handler fires.
"""

from __future__ import annotations

import datetime as dt
import sys
import threading
import time
from dataclasses import dataclass
from typing import Any

if sys.platform != "darwin":  # pragma: no cover - module is mac-only
    raise RuntimeError("rcink.sources.reminders requires macOS (EventKit)")

import EventKit as ek
import Foundation as F


@dataclass(slots=True)
class Reminder:
    title: str
    due: dt.datetime | None
    updated: dt.datetime | None
    priority: int  # 0..9; 0 = no priority


@dataclass(slots=True)
class TodaysReminders:
    list_name: str
    items: list[Reminder]  # incomplete and due today or overdue
    incomplete_total: int  # incomplete count across the list (for ratio)


def _due_to_dt(components) -> dt.datetime | None:
    if components is None:
        return None
    cal = F.NSCalendar.currentCalendar()
    date = cal.dateFromComponents_(components)
    if date is None:
        return None
    return dt.datetime.fromtimestamp(date.timeIntervalSince1970(), tz=dt.timezone.utc).astimezone()


def _nsdate_to_dt(value: Any) -> dt.datetime | None:
    if value is None:
        return None
    return dt.datetime.fromtimestamp(value.timeIntervalSince1970(), tz=dt.timezone.utc).astimezone()


def _fetch_sync(store: ek.EKEventStore, predicate) -> list:
    """Block until EventKit's async fetch completes; pump the run loop."""
    done = threading.Event()
    out: list = []

    def cb(reminders):
        if reminders:
            out.extend(list(reminders))
        done.set()

    store.fetchRemindersMatchingPredicate_completion_(predicate, cb)
    runloop = F.NSRunLoop.currentRunLoop()
    start = time.time()
    while not done.is_set() and time.time() - start < 5.0:
        runloop.runUntilDate_(F.NSDate.dateWithTimeIntervalSinceNow_(0.05))
    return out


def fetch_todays_reminders(now: dt.datetime | None = None) -> TodaysReminders:
    now = now or dt.datetime.now().astimezone()
    store = ek.EKEventStore.alloc().init()

    default_cal = store.defaultCalendarForNewReminders()
    list_name = default_cal.title() if default_cal else "Reminders"
    calendars = [default_cal] if default_cal else None

    pred = store.predicateForIncompleteRemindersWithDueDateStarting_ending_calendars_(None, None, calendars)
    raw = _fetch_sync(store, pred)

    end_of_day = dt.datetime.combine(
        now.date() + dt.timedelta(days=1),
        dt.time.min,
        tzinfo=now.tzinfo,
    )

    items: list[Reminder] = []
    for r in raw:
        title = (r.title() or "").strip()
        if not title:
            continue
        due = _due_to_dt(r.dueDateComponents())
        # Show items that are due today, overdue, or have no due date (since
        # the default list is often used for ad-hoc todos).
        if due is None or due < end_of_day:
            items.append(
                Reminder(
                    title=title,
                    due=due,
                    updated=_nsdate_to_dt(r.lastModifiedDate()) or _nsdate_to_dt(r.creationDate()),
                    priority=int(r.priority()),
                )
            )

    # Sort: most recently updated first so stale old reminders fall off the
    # page. Priority and due date only break ties.
    def sort_key(item: Reminder):
        prio = item.priority if item.priority > 0 else 10
        updated_ts = item.updated.timestamp() if item.updated is not None else float("-inf")
        due_ts = item.due.timestamp() if item.due is not None else float("inf")
        return (-updated_ts, prio, due_ts, item.title.casefold())

    items.sort(key=sort_key)

    return TodaysReminders(
        list_name=list_name,
        items=items,
        incomplete_total=len(raw),
    )
