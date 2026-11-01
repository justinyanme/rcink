"""Producer semantics, checked against the approved consumed-quota thresholds."""

import datetime as dt

import kp4
import pytest
from rcink.keypoint import Card, Row, load_cards, render_frame, write_card
from rcink.keypoint_cards import claude_card, codex_card
from rcink.sources.claude import ClaudeUsage, ScopedLimit
from rcink.sources.codex import CodexUsage

NOW = dt.datetime(2026, 10, 23, 12, tzinfo=dt.UTC)


@pytest.mark.parametrize(("pct", "role"), [(0, 1), (74.9, 1), (75, 2), (89.9, 2), (90, 3), (100, 3)])
def test_usage_roles_apply_to_each_window_and_scoped_limit(pct, role):
    usage = ClaudeUsage(pct, [ScopedLimit("FABLE", pct)], "")
    parsed = kp4.parse(render_frame(claude_card(usage, now=NOW), now=NOW, generation=0, index=0, total=1))
    assert parsed.row_roles == (0, 0, role, role, 0, 0)


def test_scoped_limit_role_does_not_change_whole_card_led_hint():
    usage = ClaudeUsage(30, [ScopedLimit("FABLE", 95)], "")
    parsed = kp4.parse(render_frame(claude_card(usage, now=NOW), now=NOW, generation=0, index=0, total=1))
    assert (parsed.led_hint, parsed.row_roles) == (0, (0, 0, 1, 3, 0, 0))


def test_single_weekly_codex_window_has_one_colored_bar():
    usage = CodexUsage(94, "4d", 10080, None, "", None, None)
    parsed = kp4.parse(render_frame(codex_card(usage, now=NOW), now=NOW, generation=0, index=0, total=1))
    assert parsed.row_roles == (0, 0, 3, 0, 0, 0)


def test_timestamp_insertion_and_padding_preserve_row_semantics(tmp_path, monkeypatch):
    monkeypatch.setenv("RCINK_KEYPOINT_CACHE", str(tmp_path))
    card = Card("test", "NONE", "TEST", 0, [Row("VAL", "9", role="error"), Row(bar=60, role="positive")])
    write_card(card)
    cached = load_cards()[0]
    parsed = kp4.parse(render_frame(cached, now=NOW, generation=0, index=0, total=1))
    assert parsed.row_roles == (0, 4, 0, 1, 0, 0)


@pytest.mark.parametrize("role", ["red", "", "WARNING", None, 2, [], {}])
def test_row_rejects_unknown_roles(role):
    with pytest.raises(ValueError, match="row role"):
        Row(text="X", role=role)
