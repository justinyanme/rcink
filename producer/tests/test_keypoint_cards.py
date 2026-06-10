"""Compact KP3 cards built from the existing source dataclasses.

Each builder must yield a renderable Card (icon in the KP3 set, 1..4 rows, all
lines <=8 ASCII chars). Reset rows carry an absolute epoch (``until``) so the
countdown stays live between 15-minute fetches instead of freezing at the
formatted string the page builders use.
"""

import datetime as dt

from rcink.keypoint import render_frame
from rcink.keypoint_cards import (
    claude_card,
    codex_card,
)
from rcink.sources.claude import ClaudeUsage
from rcink.sources.codex import CodexUsage

NOW = dt.datetime(2026, 6, 10, 12, 34, 0).astimezone()
NOW_TS = int(NOW.timestamp())
RESET_AT = NOW_TS + 90 * 60


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
        plan="Max",
    )


def test_claude_card_renders():
    card = claude_card(make_claude(), now=NOW)
    fields = _renders(card)
    assert fields[4] == "CLAUDE"
    assert fields[5] == "2"
    assert fields[6] == "CLAUDE  "
    assert fields[7] == "5H   76%"
    assert fields[9] == "7D   88%"
    assert fields[10] == "RST  90M"  # live countdown from the epoch, not "2h 14m"
    assert card.data_ts == NOW_TS


def test_claude_card_handles_missing_pct_and_reset():
    usage = ClaudeUsage(None, None, None, None, "", "", five_hour_reset_at=None)
    card = claude_card(usage, now=NOW)
    fields = _renders(card)
    assert fields[5] == "0"
    assert fields[7] == "5H    0%"
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
    assert fields[4] == "CODEX"
    assert fields[5] == "0"
    assert fields[7] == "5H   42%"
    assert fields[9] == "7D   68%"
    assert fields[10] == "RST  90M"


def test_codex_card_warns_on_high_usage():
    usage = make_codex()
    usage.primary_pct = 94.0
    fields = _renders(codex_card(usage, now=NOW))
    assert fields[5] == "3"
