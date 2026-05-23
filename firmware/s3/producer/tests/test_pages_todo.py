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


def _is_todo_row(node: dict) -> bool:
    """Rows start with the compact `todo` icon marker."""
    if node.get("type") != "hstack":
        return False
    return any(c.get("type") == "icon" and c.get("name") == "todo" for c in node.get("children", []))


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
    row = next(child for child in page["children"] if _is_todo_row(child))
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
    rows = [child for child in page["children"] if _is_todo_row(child)]
    assert len(rows) == VISIBLE_ITEMS


def test_todo_page_wraps_long_title_to_two_lines():
    now = dt.datetime(2026, 5, 18, 12, 0, tzinfo=dt.timezone.utc)
    long_title = "rewrite the producer pipeline to support multi-list reminders"
    page = build_todo_page(
        TodaysReminders(
            list_name="Inbox",
            items=[_reminder(long_title, updated_min_ago=5)],
            incomplete_total=1,
        ),
        now=now,
    )
    row = next(child for child in page["children"] if _is_todo_row(child))
    title_cell = next(c for c in row["children"] if c.get("type") == "vstack")
    line_values = [c.get("value") for c in title_cell["children"] if c.get("type") == "text"]
    assert len(line_values) == 2
    assert " ".join(line_values).split()[:3] == long_title.split()[:3]


def test_todo_page_wraps_long_cjk_title_to_two_lines():
    """CJK chars render ~2x wider than Latin; the wrapper must count cells,
    not Python chars, or long Chinese titles silently overflow."""
    now = dt.datetime(2026, 5, 18, 12, 0, tzinfo=dt.timezone.utc)
    title = "确认是否可通过用户ID获取自选股和持仓并出具持仓报告"
    page = build_todo_page(
        TodaysReminders(
            list_name="Inbox",
            items=[_reminder(title, updated_min_ago=5)],
            incomplete_total=1,
        ),
        now=now,
    )
    row = next(child for child in page["children"] if _is_todo_row(child))
    title_cell = next(c for c in row["children"] if c.get("type") == "vstack")
    line_values = [c.get("value") for c in title_cell["children"] if c.get("type") == "text"]
    assert len(line_values) == 2


def test_todo_page_url_residual_avoids_em_dash():
    """VT323 has no `—` glyph, so the inline residual must use an ASCII
    separator (or wrap to a second line)."""
    now = dt.datetime(2026, 5, 18, 12, 0, tzinfo=dt.timezone.utc)
    page = build_todo_page(
        TodaysReminders(
            list_name="Inbox",
            items=[_reminder("https://x.com/post tip", updated_min_ago=5)],
            incomplete_total=1,
        ),
        now=now,
    )
    row = next(child for child in page["children"] if _is_todo_row(child))
    # Recursively collect every text value in the row.
    stack = list(row["children"])
    seen = []
    while stack:
        node = stack.pop()
        if node.get("type") == "text":
            seen.append(node.get("value", ""))
        stack.extend(node.get("children", []) or [])
    assert not any("—" in v for v in seen), seen


def test_todo_page_keeps_short_title_on_one_line():
    now = dt.datetime(2026, 5, 18, 12, 0, tzinfo=dt.timezone.utc)
    page = build_todo_page(
        TodaysReminders(
            list_name="Inbox",
            items=[_reminder("ship it", updated_min_ago=1)],
            incomplete_total=1,
        ),
        now=now,
    )
    row = next(child for child in page["children"] if _is_todo_row(child))
    # No vstack title cell when the title fits on one line.
    assert not any(c.get("type") == "vstack" for c in row["children"])
