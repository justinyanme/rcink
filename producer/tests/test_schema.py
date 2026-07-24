import datetime as dt

import pytest

from rcink.cli_goldens import _BUILDERS, _layout_of
from rcink.layout import hbar, icon, sparkline, text, vstack
from rcink.pages.calendar import build_calendar_page
from rcink.pages.claude import build_claude_compact_page
from rcink.pages.github import build_github_page
from rcink.pages.todo import build_todo_page
from rcink.sources.calendar import CalEvent, TodaysCalendar
from rcink.sources.claude import ClaudeUsage
from rcink.sources.github import GRID_CELLS, GitHubActivity
from rcink.sources.reminders import Reminder, TodaysReminders
from rcink.validate import LayoutValidationError, validate_page

NOW = dt.datetime(2026, 1, 1, 12, 0, tzinfo=dt.UTC)


def _claude_usage() -> ClaudeUsage:
    return ClaudeUsage(
        five_hour_pct=42.0,
        seven_day_pct=15.0,
        scoped_limits=[],
        five_hour_reset="2h 14m",
        seven_day_reset="5d 4h",
    )


def _calendar() -> TodaysCalendar:
    event = CalEvent(
        start=NOW,
        end=NOW + dt.timedelta(minutes=30),
        title="Standup",
        calendar="Personal",
        is_all_day=False,
    )
    return TodaysCalendar(today=NOW.date(), events=[event], upcoming=[])


def _github() -> GitHubActivity:
    return GitHubActivity(
        user="rocry",
        today_contributions=3,
        week_contributions=18,
        streak=5,
        total_14w=142,
        total_year=420,
        daily_counts=[0] * (GRID_CELLS - 4) + [1, 5, 2, 8],
    )


def _reminders() -> TodaysReminders:
    return TodaysReminders(
        list_name="Inbox",
        items=[Reminder(title="ship it", due=None, updated=NOW, priority=1)],
        incomplete_total=1,
    )


@pytest.mark.parametrize("page,builder", sorted(_BUILDERS.items()))
def test_golden_builders_validate(page, builder):
    validate_page(_layout_of(builder()))


@pytest.mark.parametrize(
    "name,layout",
    [
        ("claude_compact", build_claude_compact_page(_claude_usage())),
        ("calendar", build_calendar_page(_calendar(), now=NOW)),
        ("github", build_github_page(_github(), now=NOW)),
        ("todo", build_todo_page(_reminders(), now=NOW)),
    ],
)
def test_non_golden_page_builders_validate(name, layout):
    validate_page(layout)


def test_rejects_envelope_instead_of_bare_layout_root():
    with pytest.raises(LayoutValidationError, match="<root>"):
        validate_page({"version": 1, "id": "x", "layout": vstack(children=[])})


def test_rejects_unknown_property():
    layout = vstack(children=[text("X")])
    layout["children"][0]["max_lines"] = 2
    with pytest.raises(LayoutValidationError, match="max_lines"):
        validate_page(layout)


def test_rejects_invalid_node_type():
    with pytest.raises(LayoutValidationError, match="not valid"):
        validate_page(vstack(children=[{"type": "nope"}]))


def test_rejects_root_non_stack():
    with pytest.raises(LayoutValidationError, match="children"):
        validate_page(text("X"))


def test_rejects_over_limit_labels():
    layout = vstack(children=[hbar(pct=42, label_left="x" * 25)])
    with pytest.raises(LayoutValidationError, match="label_left"):
        validate_page(layout)


def test_rejects_uppercase_enums():
    layout = vstack(children=[text("X", size="body")])
    layout["children"][0]["size"] = "Body"
    with pytest.raises(LayoutValidationError, match="size"):
        validate_page(layout)


def test_rejects_over_limit_text_value():
    with pytest.raises(LayoutValidationError, match="value"):
        validate_page(vstack(children=[text("x" * 129)]))


def test_rejects_over_limit_stack_children():
    with pytest.raises(LayoutValidationError, match="children"):
        validate_page(vstack(children=[text(str(i)) for i in range(33)]))


def test_rejects_over_limit_sparkline_samples():
    with pytest.raises(LayoutValidationError, match="values"):
        validate_page(vstack(children=[sparkline([float(i) for i in range(65)])]))


def test_rejects_over_limit_icon_name():
    with pytest.raises(LayoutValidationError, match="name"):
        validate_page(vstack(children=[icon("x" * 17)]))
