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


def test_claude_card_handles_missing_pct_and_reset():
    usage = ClaudeUsage(None, None, None, None, "", "", five_hour_reset_at=None)
    card = claude_card(usage, now=NOW)
    fields = _renders(card)
    assert fields[5] == "0"
    assert fields[7] == "5H    0%"  # fallback: pct text when no reset_at
    assert fields[8] == "[000]"  # bar at 0%
    assert card.rows  # still renderable


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
