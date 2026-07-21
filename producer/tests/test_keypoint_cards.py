"""Compact KP3 cards built from the existing source dataclasses.

Each builder must yield a renderable Card (icon in the KP3 set, 1..4 rows, all
lines <=8 ASCII chars). Reset rows carry an absolute epoch (``until``) so the
countdown stays live between 15-minute fetches instead of freezing at the
formatted string the page builders use.
"""

import datetime as dt

import pytest
from rcink.keypoint import render_frame
from rcink.keypoint_cards import (
    calendar_card,
    claude_card,
    codex_card,
    todo_card,
    weather_card,
)
from rcink.sources.calendar import CalEvent, TodaysCalendar
from rcink.sources.claude import ClaudeUsage
from rcink.sources.codex import CodexUsage
from rcink.sources.reminders import Reminder, TodaysReminders
from rcink.sources.weather import Weather

NOW = dt.datetime(2026, 6, 10, 12, 34, 0).astimezone()
NOW_TS = int(NOW.timestamp())
RESET_AT = NOW_TS + 90 * 60  # 5h window: 90 min remaining
RESET_AT_7D = NOW_TS + 4 * 86400  # 7d window: 4 days remaining


def _renders(card) -> list[str]:
    """Every builder output must pass the wire validator; return the 12 fields
    (KP3, GEN, IDX, TOTAL, ICON, LED, L1..L6)."""
    return render_frame(card, now=NOW, generation=0xA0, index=0, total=1).decode("ascii").split("|")


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
        seven_day_reset_at=RESET_AT_7D,
        plan="Max",
    )


def test_claude_card_renders():
    card = claude_card(make_claude(), now=NOW)
    fields = _renders(card)
    assert fields[4] == "CLAUDE"
    assert fields[5] == "2"  # ATTENTION: peak >= 75%
    assert fields[6] == "CLAUDE  "
    assert fields[7] == "5H 1h30m"  # L2: 5H countdown to reset
    assert fields[8] == "[076]"  # L3: 5H bar at 76%
    assert fields[9] == "7D    4d"  # L4: 7D countdown to reset
    assert fields[10] == "[088]"  # L5: 7D bar at 88%
    assert fields[11] == ""  # L6: empty
    assert card.data_ts == NOW_TS


def test_claude_card_rejects_missing_required_pct():
    usage = ClaudeUsage(None, None, None, None, "", "", five_hour_reset_at=None)
    with pytest.raises(ValueError, match="usage pct"):
        claude_card(usage, now=NOW)


def frame_text(fields: list[str]) -> str:
    return "".join(fields)


# --- codex ---


RESET_AT_SECONDARY = NOW_TS + 6 * 86400  # secondary window: 6 days remaining


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
        secondary_reset_at=RESET_AT_SECONDARY,
        plan="Plus",
    )


def test_codex_card_renders():
    fields = _renders(codex_card(make_codex(), now=NOW))
    assert fields[4] == "CODEX"
    assert fields[5] == "0"
    assert fields[7] == "5H 1h30m"  # L2: primary countdown to reset
    assert fields[8] == "[042]"  # L3: primary bar at 42%
    assert fields[9] == "7D    6d"  # L4: secondary countdown to reset
    assert fields[10] == "[068]"  # L5: secondary bar at 68%


def test_codex_card_warns_on_high_usage():
    usage = make_codex()
    usage.primary_pct = 94.0
    fields = _renders(codex_card(usage, now=NOW))
    assert fields[5] == "3"


def test_codex_card_renders_single_weekly_window():
    usage = CodexUsage(
        primary_pct=12.0,
        primary_reset="4d",
        primary_window_min=10080,
        secondary_pct=None,
        secondary_reset="",
        secondary_window_min=None,
        credits_dollars=None,
        primary_reset_at=RESET_AT_7D,
        secondary_reset_at=None,
        plan="Pro",
    )

    fields = _renders(codex_card(usage, now=NOW))

    assert fields[7] == "7D    4d"
    assert fields[8] == "[012]"
    assert fields[9] == ""
    assert fields[10] == ""


# --- weather ---


def test_weather_card_renders():
    weather = Weather(
        location="LONDON",
        temp=28.4,
        high=31.2,
        low=24.6,
        unit="C",
        code=80,
        precip_probability=67,
        is_day=True,
        observed_at=NOW,
    )

    fields = _renders(weather_card(weather, now=NOW))

    assert fields[4] == "RAIN"
    assert fields[5] == "2"
    assert fields[6] == "WEATHER "
    assert fields[7] == "T    28C"
    assert fields[9] == "H    31C"
    assert fields[10] == "L    25C"
    assert fields[11] == "POP  67%"


# --- calendar ---


def _event(start: dt.datetime, title: str, *, all_day: bool = False) -> CalEvent:
    return CalEvent(
        start=start,
        end=start + dt.timedelta(hours=1),
        title=title,
        calendar="Work",
        is_all_day=all_day,
    )


def test_calendar_card_uses_live_countdown():
    event = _event(NOW + dt.timedelta(minutes=90), "Standup")
    card = calendar_card(TodaysCalendar(today=NOW.date(), events=[event], upcoming=[]), now=NOW)
    fields = _renders(card)

    assert fields[4] == "TIME"
    assert fields[5] == "1"
    assert fields[6] == "CAL     "
    assert fields[7] == "AT 14:04"
    assert fields[9] == "IN 1h30m"
    assert fields[10] == "EV Stand"


def test_calendar_card_sanitizes_non_ascii_title():
    event = _event(NOW + dt.timedelta(minutes=10), "会议|Sync")
    fields = _renders(calendar_card(TodaysCalendar(today=NOW.date(), events=[event], upcoming=[]), now=NOW))
    assert fields[10] == "EV ???Sy"


# --- todo ---


def test_todo_card_renders_top_item_and_due_countdown():
    due = NOW + dt.timedelta(hours=2)
    rem = TodaysReminders(
        list_name="Inbox",
        items=[Reminder(title="Ship phase 6", due=due, updated=NOW, priority=0)],
        incomplete_total=8,
    )

    fields = _renders(todo_card(rem, now=NOW))

    assert fields[4] == "WARN"
    assert fields[5] == "2"
    assert fields[6] == "TODO    "
    assert fields[7] == "CNT    8"
    assert fields[9] == "TOP Ship"
    assert fields[10] == "D     2h"


def test_todo_card_all_clear():
    rem = TodaysReminders(list_name="Inbox", items=[], incomplete_total=0)
    fields = _renders(todo_card(rem, now=NOW))
    assert fields[4] == "NONE"
    assert fields[5] == "0"
    assert fields[7] == "CNT    0"
    assert fields[9] == "OK   YES"
