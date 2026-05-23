from rcink.pages.claude import build_claude_page
from rcink.sources.claude import ClaudeUsage


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
    page = build_claude_page(make_usage(), sparkline=None)
    assert page["type"] == "vstack"
    assert "children" in page


def test_header_is_red_knockout_title():
    page = build_claude_page(make_usage(), sparkline=None)
    header = page["children"][0]
    assert header["type"] == "text"
    assert header["value"] == "CLAUDE"
    assert header["color"] == "white"
    assert header["bg"] == "red"


def test_no_brand_footer_row():
    """Brand footer moved into the firmware-painted nav strip."""
    page = build_claude_page(make_usage(), sparkline=None)
    for child in page["children"]:
        if child.get("type") != "hstack":
            continue
        inner = child.get("children") or []
        assert not (inner and inner[0].get("type") == "icon" and inner[0].get("name") == "claude"), (
            "claude brand row should be deleted from the page footer"
        )


def test_sparkline_only_added_when_present():
    no_spark = build_claude_page(make_usage(), sparkline=None)
    with_spark = build_claude_page(make_usage(), sparkline=[1, 2, 3, 4, 5, 6, 7])
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
    page = build_claude_page(usage, sparkline=None)
    # Per-model row should be omitted entirely
    text_values = [c.get("value") for c in page["children"] if c["type"] == "text"]
    assert "BY MODEL · 7D" not in text_values
