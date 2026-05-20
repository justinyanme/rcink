"""Compact 250x128 Todo page from macOS Reminders."""

import datetime as dt

from ..layout import hbar, text, vstack
from ..sources.todo import Reminder, TodaysReminders, fetch_todays_reminders

VISIBLE_ITEMS = 2


def _trim(value: str, limit: int) -> str:
    return value if len(value) <= limit else value[: limit - 3].rstrip() + "..."


def _priority_prefix(item: Reminder) -> str:
    if 0 < item.priority <= 3:
        return "! "
    return ""


def _relative(when: dt.datetime | None, *, now: dt.datetime) -> str:
    if when is None:
        return ""
    seconds = max(0, int((now - when).total_seconds()))
    if seconds < 60:
        return "now"
    minutes = seconds // 60
    if minutes < 60:
        return f"{minutes}m"
    hours = minutes // 60
    if hours < 24:
        return f"{hours}h"
    return f"{hours // 24}d"


def _item_line(item: Reminder, *, now: dt.datetime) -> dict:
    suffix = _relative(item.updated, now=now)
    title = _trim(item.title, 28)
    value = f"[ ] {_priority_prefix(item)}{title}"
    if suffix:
        value = f"{value} {suffix}"
    return text(value, size="sm", color="red" if 0 < item.priority <= 3 else "black")


def build_page(
    reminders: TodaysReminders | None = None,
    *,
    now: dt.datetime | None = None,
) -> dict:
    now = now or dt.datetime.now().astimezone()
    reminders = reminders or fetch_todays_reminders(now=now)
    total = max(1, reminders.incomplete_total)
    done_pct = int(100 * (1 - len(reminders.items) / total))
    visible = reminders.items[:VISIBLE_ITEMS]
    lines = (
        [_item_line(item, now=now) for item in visible]
        if visible
        else [text("ALL CLEAR", size="body", weight="bold", align="center")]
    )

    return vstack(
        pad=4,
        gap=3,
        children=[
            text(
                "TODO",
                size="h1",
                color="white",
                bg="red",
                pad=4,
                align="center",
                weight="bold",
            ),
            hbar(
                pct=done_pct,
                color="black" if done_pct >= 50 else "red",
                height=14,
                label_left=reminders.list_name.upper(),
                label_right=f"{len(reminders.items)}/{reminders.incomplete_total}",
            ),
            *lines,
        ],
    )
