import datetime as dt

from rcpaper.pages.todo import VISIBLE_ITEMS, build_todo_page
from rcpaper.sources.reminders import Reminder, TodaysReminders


def _reminder(title: str, *, updated_min_ago: int, priority: int = 0) -> Reminder:
    now = dt.datetime(2026, 5, 18, 12, 0, tzinfo=dt.timezone.utc)
    return Reminder(
        title=title,
        due=None,
        updated=now - dt.timedelta(minutes=updated_min_ago),
        priority=priority,
    )


def test_todo_page_shows_recent_items_with_relative_time():
    now = dt.datetime(2026, 5, 18, 12, 0, tzinfo=dt.timezone.utc)
    page = build_todo_page(
        TodaysReminders(
            list_name="Inbox",
            items=[_reminder("ship it", updated_min_ago=90, priority=1)],
            incomplete_total=12,
        ),
        now=now,
    )
    row = next(
        child
        for child in page["children"]
        if child.get("type") == "hstack" and any(grand.get("value") == "[ ]" for grand in child.get("children", []))
    )
    text_values = [child.get("value") for child in row["children"] if child.get("type") == "text"]
    assert "ship it" in text_values
    assert "1h" in text_values


def test_todo_page_caps_visible_rows():
    now = dt.datetime(2026, 5, 18, 12, 0, tzinfo=dt.timezone.utc)
    items = [_reminder(f"task {i}", updated_min_ago=i) for i in range(VISIBLE_ITEMS + 3)]
    page = build_todo_page(
        TodaysReminders(
            list_name="Inbox",
            items=items,
            incomplete_total=len(items),
        ),
        now=now,
    )
    rows = [
        child
        for child in page["children"]
        if child.get("type") == "hstack" and any(grand.get("value") == "[ ]" for grand in child.get("children", []))
    ]
    assert len(rows) == VISIBLE_ITEMS
