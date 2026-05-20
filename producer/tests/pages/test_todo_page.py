import datetime as dt
import json
from pathlib import Path

from tlsr_layout.pages.todo import build_page
from tlsr_layout.sources.todo import Reminder, TodaysReminders

from .test_colors import assert_bwr_only

FIXTURES = Path(__file__).parent / "fixtures"
GOLDEN = Path(__file__).parent / "golden"


def _dt(value: str | None) -> dt.datetime | None:
    return None if value is None else dt.datetime.fromisoformat(value)


def test_todo_page_matches_golden():
    raw = json.loads((FIXTURES / "todo.json").read_text())
    reminders = TodaysReminders(
        list_name=raw["list_name"],
        incomplete_total=raw["incomplete_total"],
        items=[
            Reminder(
                title=item["title"],
                due=_dt(item["due"]),
                updated=_dt(item["updated"]),
                priority=item["priority"],
            )
            for item in raw["items"]
        ],
    )

    page = build_page(reminders, now=_dt(raw["now"]))

    assert page == json.loads((GOLDEN / "todo.json").read_text())
    assert_bwr_only(page)
