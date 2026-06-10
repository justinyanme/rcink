"""Build compact KP2 cards from the existing source dataclasses.

Each builder turns one source's rich data into a renderable :class:`Card`:
icon + 8-char title + up to 4 grid rows + the fetch timestamp. Reset rows use
``Row(until=epoch)`` so the keypoint-push job renders a live countdown instead
of the frozen "2h 14m" string the e-ink page builders show.

Calendar and reminder titles are Chinese and the KP2 font is ASCII-only, so
those cards surface counts and the next event time, never the item text.
"""

from __future__ import annotations

import datetime as dt

from rcink.keypoint import LINE_MAX, Card, Row
from rcink.sources.calendar import TodaysCalendar
from rcink.sources.claude import ClaudeUsage
from rcink.sources.codex import CodexUsage
from rcink.sources.github import GitHubActivity
from rcink.sources.reminders import TodaysReminders


def _pct(value: float | None) -> str:
    return f"{int(value or 0)}%"


def _count(n: int, budget: int) -> str:
    """Format a count into at most ``budget`` chars, compressing with a K/M
    suffix as needed (a multi-year streak or a 1000+ todo backlog must not
    overflow the grid and crash the unattended fetcher via kv())."""
    for value in (str(n), f"{n // 1000}K", f"{n // 1_000_000}M"):
        if len(value) <= budget:
            return value
    return "9" * budget  # absurd magnitude: clamp rather than overflow


def _value_budget(label: str, *, suffix: str = "") -> int:
    """Chars left for a value beside ``label`` on one 8-char line (kv needs a
    1-space gap), minus any fixed suffix the value carries."""
    return LINE_MAX - 1 - len(label) - len(suffix)


def claude_card(usage: ClaudeUsage, *, now: dt.datetime) -> Card:
    rows = [Row("5H", _pct(usage.five_hour_pct)), Row("7D", _pct(usage.seven_day_pct))]
    if usage.five_hour_reset_at is not None:
        rows.append(Row("RST", until=usage.five_hour_reset_at))
    rows.append(Row("OP", _pct(usage.opus_pct)))
    return Card(id="claude", icon="CLAUDE", title="CLAUDE", rows=rows, data_ts=int(now.timestamp()))


def codex_card(usage: CodexUsage, *, now: dt.datetime) -> Card:
    rows = [Row("5H", _pct(usage.primary_pct)), Row("7D", _pct(usage.secondary_pct))]
    if usage.primary_reset_at is not None:
        rows.append(Row("RST", until=usage.primary_reset_at))
    return Card(id="codex", icon="CODEX", title="CODEX", rows=rows, data_ts=int(now.timestamp()))


def github_card(activity: GitHubActivity, *, now: dt.datetime) -> Card:
    rows = [
        Row("TDY", _count(activity.today_commits, _value_budget("TDY"))),
        Row("WK", _count(activity.week_commits, _value_budget("WK"))),
        Row("STK", f"{_count(activity.streak, _value_budget('STK', suffix='D'))}D"),
    ]
    return Card(id="github", icon="CODE", title="GITHUB", rows=rows, data_ts=int(now.timestamp()))


def calendar_card(cal: TodaysCalendar, *, now: dt.datetime) -> Card:
    rows = [Row("EV", _count(len(cal.events), _value_budget("EV")))]
    upcoming = [e for e in cal.events if e.start > now]
    if upcoming:
        rows.append(Row("NXT", min(upcoming, key=lambda e: e.start).start.strftime("%H%M")))
    return Card(id="calendar", icon="TIME", title="CAL", rows=rows, data_ts=int(now.timestamp()))


def todo_card(rem: TodaysReminders, *, now: dt.datetime) -> Card:
    overdue = any(item.due is not None and item.due < now for item in rem.items)
    rows = [
        Row("DUE", _count(len(rem.items), _value_budget("DUE"))),
        Row("OPEN", _count(rem.incomplete_total, _value_budget("OPEN"))),
    ]
    return Card(
        id="todo",
        icon="WARN" if overdue else "NONE",
        title="TODO",
        rows=rows,
        data_ts=int(now.timestamp()),
    )
