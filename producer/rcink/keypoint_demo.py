"""A demo KEYPOINT deck: fixed sample data through the real card builders.

`keypoint-push --demo` shows what the producer's cards look like without live
sources, so a layout change can be checked on the glass. The sample values are
arbitrary but chosen to light up each card's interesting rows (a scoped limit,
two Codex windows, rain, a due todo). Times are relative to `now` so countdowns
read sensibly whenever it runs.
"""

from __future__ import annotations

import datetime as dt

from rcink.keypoint import Card
from rcink.keypoint_cards import (
    calendar_card,
    claude_card,
    codex_card,
    todo_card,
    weather_card,
)
from rcink.sources.calendar import CalEvent, TodaysCalendar
from rcink.sources.claude import ClaudeUsage, ScopedLimit
from rcink.sources.codex import CodexUsage
from rcink.sources.reminders import Reminder, TodaysReminders
from rcink.sources.weather import Weather


def demo_cards(*, now: dt.datetime) -> list[Card]:
    ts = int(now.timestamp())
    meeting = now + dt.timedelta(minutes=90)
    return [
        claude_card(
            ClaudeUsage(
                seven_day_pct=68.0,
                scoped_limits=[ScopedLimit(label="FABLE", pct=23.0)],
                seven_day_reset="3d",
                seven_day_reset_at=ts + 3 * 86400,
            ),
            now=now,
        ),
        codex_card(
            CodexUsage(
                primary_pct=42.0,
                primary_reset="1h 30m",
                primary_window_min=300,
                secondary_pct=81.0,
                secondary_reset="5d",
                secondary_window_min=10080,
                credits_dollars=None,
                primary_reset_at=ts + 90 * 60,
                secondary_reset_at=ts + 5 * 86400,
            ),
            now=now,
        ),
        weather_card(
            Weather(
                location="SHENZHEN",
                temp=27.6,
                high=30.1,
                low=23.4,
                unit="C",
                code=61,
                precip_probability=70,
                is_day=True,
                observed_at=now,
            ),
            now=now,
        ),
        calendar_card(
            TodaysCalendar(
                today=now.date(),
                events=[
                    CalEvent(
                        start=meeting,
                        end=meeting + dt.timedelta(minutes=30),
                        title="Design review",
                        calendar="Work",
                        is_all_day=False,
                    )
                ],
                upcoming=[],
            ),
            now=now,
        ),
        todo_card(
            TodaysReminders(
                list_name="Inbox",
                items=[
                    Reminder(
                        title="Ship KP4 demo",
                        due=now + dt.timedelta(hours=2),
                        updated=now,
                        priority=0,
                    )
                ],
                incomplete_total=5,
            ),
            now=now,
        ),
    ]
