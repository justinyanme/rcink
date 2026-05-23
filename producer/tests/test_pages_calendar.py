import datetime as dt

from rcink.pages.calendar import build_calendar_page
from rcink.sources.calendar import CalEvent, TodaysCalendar


def _ev(when_str: str, title: str, *, duration_min: int = 30, all_day: bool = False) -> CalEvent:
    start = dt.datetime.fromisoformat(when_str)
    return CalEvent(
        start=start,
        end=start + dt.timedelta(minutes=duration_min),
        title=title,
        calendar="Personal",
        is_all_day=all_day,
    )


def _all_text(node: dict) -> list[str]:
    out: list[str] = []
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == "text":
            out.append(n.get("value", ""))
        stack.extend(n.get("children", []) or [])
    return out


def test_calendar_today_has_events_unchanged():
    now = dt.datetime(2026, 5, 23, 9, 0)
    cal = TodaysCalendar(
        today=now.date(),
        events=[_ev("2026-05-23T10:00", "Standup")],
        upcoming=[],
    )
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    assert "NEXT" in texts
    assert "Standup" in texts


def test_calendar_today_empty_with_upcoming_renders_upcoming_section():
    now = dt.datetime(2026, 5, 23, 9, 0)  # Saturday
    cal = TodaysCalendar(
        today=now.date(),
        events=[],
        upcoming=[
            _ev("2026-05-25T09:00", "Standup"),  # Monday
            _ev("2026-05-26T14:00", "1:1 with Sam"),  # Tuesday
        ],
    )
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    assert "NO EVENTS" in texts
    assert "UPCOMING" in texts
    assert "Standup" in texts
    assert "1:1 with Sam" in texts
    # DOW prefix present.
    assert any(t.startswith("MON") for t in texts)
    assert any(t.startswith("TUE") for t in texts)


def test_calendar_empty_week_renders_nothing_until_next_week():
    now = dt.datetime(2026, 5, 23, 9, 0)
    cal = TodaysCalendar(today=now.date(), events=[], upcoming=[])
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    assert any("NOTHING UNTIL NEXT WEEK" in t for t in texts)


def test_calendar_upcoming_caps_at_six_rows():
    now = dt.datetime(2026, 5, 23, 9, 0)
    upcoming = [_ev(f"2026-05-25T{9 + i:02d}:00", f"event {i}") for i in range(10)]
    cal = TodaysCalendar(today=now.date(), events=[], upcoming=upcoming)
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    # Only 6 of the titles should appear.
    visible_titles = [t for t in texts if t.startswith("event ")]
    assert len(visible_titles) == 6


def test_calendar_upcoming_all_day_event_uses_ALL():
    now = dt.datetime(2026, 5, 23, 9, 0)
    cal = TodaysCalendar(
        today=now.date(),
        events=[],
        upcoming=[_ev("2026-05-26T00:00", "Conference", all_day=True)],
    )
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    assert any(t.startswith("TUE") and "ALL" in t for t in texts)
