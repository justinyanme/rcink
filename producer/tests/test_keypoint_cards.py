"""Compact KP2 cards built from the existing source dataclasses.

Each builder must yield a renderable Card (icon in the KP2 set, 1..4 rows, all
lines <=8 ASCII chars). Reset rows carry an absolute epoch (``until``) so the
countdown stays live between 15-minute fetches instead of freezing at the
formatted string the page builders use.
"""

import datetime as dt

from rcink.keypoint import render_frame
from rcink.keypoint_cards import (
    claude_card,
    codex_card,
    github_card,
)
from rcink.sources.claude import ClaudeUsage
from rcink.sources.codex import CodexUsage
from rcink.sources.github import GitHubActivity

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
