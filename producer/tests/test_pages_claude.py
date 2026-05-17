import datetime as dt

from rcpaper.pages.claude import build_claude_page
from rcpaper.sources.claude import ClaudeUsage


def make_usage():
    return ClaudeUsage(
        five_hour_pct=42.0,
        seven_day_pct=68.0,
        opus_pct=18.0,
        sonnet_pct=24.0,
        five_hour_reset="2h 14m",
        seven_day_reset="3d 5h",
        plan="Max",
    )


def test_root_is_vstack():
    page = build_claude_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17, 14, 32))
    assert page["type"] == "vstack"
    assert "children" in page


def test_header_is_red_knockout_title():
    page = build_claude_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17, 14, 32))
    header = page["children"][0]
    assert header["type"] == "text"
    assert header["value"] == "CLAUDE"
    assert header["color"] == "white"
    assert header["bg"] == "red"


def test_footer_includes_updated_and_plan():
    page = build_claude_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17, 14, 32))
    footer_text = page["children"][-1]
    assert footer_text["type"] == "text"
    assert "14:32" in footer_text["value"]
    assert "Max" in footer_text["value"]


def test_sparkline_only_added_when_present():
    no_spark = build_claude_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17))
    with_spark = build_claude_page(make_usage(), sparkline=[1, 2, 3, 4, 5, 6, 7], updated_at=dt.datetime(2026, 5, 17))
    assert not any(c["type"] == "sparkline" for c in no_spark["children"])
    sparkline_nodes = [c for c in with_spark["children"] if c["type"] == "sparkline"]
    assert len(sparkline_nodes) == 1
    assert sparkline_nodes[0]["values"] == [1, 2, 3, 4, 5, 6, 7]


def test_missing_model_data_skipped():
    usage = ClaudeUsage(
        five_hour_pct=42.0,
        seven_day_pct=68.0,
        opus_pct=None,
        sonnet_pct=None,
        five_hour_reset="2h",
        seven_day_reset="3d",
    )
    page = build_claude_page(usage, sparkline=None, updated_at=dt.datetime(2026, 5, 17))
    # Per-model row should be omitted entirely
    text_values = [c.get("value") for c in page["children"] if c["type"] == "text"]
    assert "BY MODEL · 7D" not in text_values
