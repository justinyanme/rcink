import datetime as dt
import json
from pathlib import Path

from tlsr_layout.pages.calendar import build_page
from tlsr_layout.sources.calendar import CalEvent, TodaysCalendar

from .test_colors import assert_bwr_only

FIXTURES = Path(__file__).parent / "fixtures"
GOLDEN = Path(__file__).parent / "golden"


def _dt(value: str) -> dt.datetime:
    return dt.datetime.fromisoformat(value)


def test_calendar_page_matches_golden():
    raw = json.loads((FIXTURES / "calendar.json").read_text())
    calendar = TodaysCalendar(
        today=dt.date.fromisoformat(raw["today"]),
        events=[
            CalEvent(
                start=_dt(item["start"]),
                end=_dt(item["end"]),
                title=item["title"],
                calendar=item["calendar"],
                is_all_day=item["is_all_day"],
            )
            for item in raw["events"]
        ],
    )

    page = build_page(calendar, now=_dt(raw["now"]))

    assert page == json.loads((GOLDEN / "calendar.json").read_text())
    assert_bwr_only(page)
