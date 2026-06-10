"""Compact KP2 cards built from the existing source dataclasses.

Each builder must yield a renderable Card (icon in the KP2 set, 1..4 rows, all
lines <=8 ASCII chars). Reset rows carry an absolute epoch (``until``) so the
countdown stays live between 15-minute fetches instead of freezing at the
formatted string the page builders use.
"""

import datetime as dt

from rcink.keypoint import render_frame
from rcink.keypoint_cards import (
    calendar_card,
    claude_card,
    codex_card,
    github_card,
    todo_card,
)
from rcink.sources.calendar import CalEvent, TodaysCalendar
from rcink.sources.claude import ClaudeUsage
from rcink.sources.codex import CodexUsage
from rcink.sources.github import GitHubActivity
from rcink.sources.reminders import Reminder, TodaysReminders

NOW = dt.datetime(2026, 6, 10, 12, 34, 0).astimezone()
NOW_TS = int(NOW.timestamp())
RESET_AT = NOW_TS + 90 * 60


def _renders(card) -> list[str]:
    """Every builder output must pass the wire validator; return the 8 fields."""
    return render_frame(card, now=NOW).decode("ascii").split("|")


# --- claude ---


def make_claude() -> ClaudeUsage:
    return ClaudeUsage(
        five_hour_pct=76.0,
        seven_day_pct=88.0,
        opus_pct=18.0,
        sonnet_pct=24.0,
        five_hour_reset="2h 14m",
        seven_day_reset="3d 5h",
        five_hour_reset_at=RESET_AT,
        plan="Max",
    )


def test_claude_card_renders():
    card = claude_card(make_claude(), now=NOW)
    fields = _renders(card)
    assert fields[1] == "CLAUDE"
    assert fields[2] == "CLAUDE  "
    assert fields[3] == "5H   76%"
    assert fields[5] == "7D   88%"
    assert fields[6] == "RST  90M"  # live countdown from the epoch, not "2h 14m"
    assert card.data_ts == NOW_TS


def test_claude_card_handles_missing_pct_and_reset():
    usage = ClaudeUsage(None, None, None, None, "", "", five_hour_reset_at=None)
    card = claude_card(usage, now=NOW)
    fields = _renders(card)
    assert fields[3] == "5H    0%"
    # No epoch -> drop the RST row rather than render a fake countdown.
    assert "RST" not in frame_text(fields)
    assert card.rows  # still renderable


def frame_text(fields: list[str]) -> str:
    return "".join(fields)


# --- codex ---


def make_codex() -> CodexUsage:
    return CodexUsage(
        primary_pct=42.0,
        primary_reset="2h",
        primary_window_min=300,
        secondary_pct=68.0,
        secondary_reset="3d",
        secondary_window_min=10080,
        credits_dollars=18.42,
        primary_reset_at=RESET_AT,
        plan="Plus",
    )


def test_codex_card_renders():
    fields = _renders(codex_card(make_codex(), now=NOW))
    assert fields[1] == "CODEX"
    assert fields[3] == "5H   42%"
    assert fields[5] == "7D   68%"
    assert fields[6] == "RST  90M"


# --- github ---


def make_github() -> GitHubActivity:
    return GitHubActivity(
        user="rocry",
        today_commits=3,
        week_commits=27,
        streak=5,
        total_14w=400,
        total_year=1200,
        contrib=[1, 2, 3],
    )


def test_github_card_renders():
    fields = _renders(github_card(make_github(), now=NOW))
    assert fields[1] == "CODE"
    assert fields[2] == "GITHUB  "
    assert fields[3] == "TDY    3"
    assert fields[5] == "WK    27"
    assert fields[6] == "STK   5D"


def test_github_card_clamps_huge_counts_to_fit():
    fields = _renders(github_card(make_github_big(), now=NOW))
    # value column never overflows the 8-char line even at 4-5 digit counts
    for line in fields[2:]:
        assert len(line) <= 8


def make_github_big() -> GitHubActivity:
    return GitHubActivity("rocry", 1234, 56789, 365, 9999, 99999, [])


def test_github_card_survives_multi_year_streak():
    """STK label leaves a 4-char value budget; a 1000+ day streak must compress
    (K-suffix) instead of crashing the unattended fetcher via kv()."""
    card = github_card(GitHubActivity("rocry", 0, 0, 1500, 0, 0, []), now=NOW)
    fields = _renders(card)  # must not raise
    assert fields[6] == "STK  1KD"


def test_todo_card_survives_huge_open_count():
    """OPEN is a 4-char label -> 3-char value budget; 1000+ must compress."""
    fields = _renders(todo_card(make_todo([], incomplete_total=1500), now=NOW))
    assert fields[5] == "OPEN  1K"


# --- calendar ---


def _event(hour: int, title: str = "Meeting") -> CalEvent:
    start = NOW.replace(hour=hour, minute=0, second=0, microsecond=0)
    return CalEvent(start=start, end=start + dt.timedelta(hours=1), title=title, calendar="Work", is_all_day=False)


def make_calendar(events: list[CalEvent]) -> TodaysCalendar:
    return TodaysCalendar(today=NOW.date(), events=events, upcoming=[])


def test_calendar_card_shows_count_and_next_time():
    cal = make_calendar([_event(9), _event(15), _event(18)])
    fields = _renders(calendar_card(cal, now=NOW))
    assert fields[1] == "TIME"
    assert fields[2] == "CAL     "
    assert fields[3] == "EV     3"
    assert fields[5] == "NXT 1500"  # first event starting after 12:34


def test_calendar_card_no_events():
    fields = _renders(calendar_card(make_calendar([]), now=NOW))
    assert fields[3] == "EV     0"
    # no NXT row when nothing is upcoming today
    assert fields[5] == ""


# --- todo ---


def make_todo(items: list[Reminder], incomplete_total: int) -> TodaysReminders:
    return TodaysReminders(list_name="Inbox", items=items, incomplete_total=incomplete_total)


def _reminder(overdue: bool) -> Reminder:
    due = NOW - dt.timedelta(hours=2) if overdue else NOW + dt.timedelta(hours=2)
    return Reminder(title="买菜", due=due, updated=NOW, priority=0)


def test_todo_card_due_and_open_counts():
    rem = make_todo([_reminder(False), _reminder(False)], incomplete_total=12)
    fields = _renders(todo_card(rem, now=NOW))
    assert fields[2] == "TODO    "
    assert fields[3] == "DUE    2"
    assert fields[5] == "OPEN  12"


def test_todo_card_warns_on_overdue():
    rem = make_todo([_reminder(overdue=True)], incomplete_total=3)
    assert todo_card(rem, now=NOW).icon == "WARN"


def test_todo_card_no_warn_when_nothing_overdue():
    rem = make_todo([_reminder(overdue=False)], incomplete_total=3)
    assert todo_card(rem, now=NOW).icon == "NONE"
