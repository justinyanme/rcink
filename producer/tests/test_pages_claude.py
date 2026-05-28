from rcink.pages.claude import build_claude_compact_page, build_claude_page
from rcink.sources.claude import ClaudeUsage


def _count_nodes(node: dict) -> int:
    return 1 + sum(_count_nodes(c) for c in node.get("children", []))


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


def test_compact_page_is_small_enough_for_tlsr_arena():
    """The 250x128 TLSR firmware parses + lays out in a fixed 4 KB arena; the full
    page overflows it (run_layout fails). Keep the compact page an order smaller."""
    page = build_claude_compact_page(make_usage())
    assert page["type"] == "vstack"
    assert page["align"] == "center"  # header chip + reset line must center
    assert _count_nodes(page) <= 12
    assert _count_nodes(page) < _count_nodes(build_claude_page(make_usage(), sparkline=None))


def test_compact_page_shows_both_usage_windows():
    """Each window is a row: [label][bar][percent], with label/percent as plain
    text (legible black-on-white) rather than draw_hbar's white-on-fill labels."""
    rows = [c for c in build_claude_compact_page(make_usage())["children"] if c["type"] == "hstack"]
    assert len(rows) == 2
    for row, label, pct in zip(rows, ("5H", "7D"), ("42%", "68%"), strict=True):
        kinds = [c["type"] for c in row["children"]]
        assert kinds == ["text", "hbar", "text"]
        assert row["children"][0]["value"] == label
        assert row["children"][2]["value"] == pct


def test_compact_bar_turns_red_only_when_window_nearly_full():
    rows = [c for c in build_claude_compact_page(make_usage()).get("children", []) if c["type"] == "hstack"]
    assert rows[0]["children"][1]["color"] == "black"  # 42% → black
    near_full = ClaudeUsage(
        five_hour_pct=92.0,
        seven_day_pct=10.0,
        opus_pct=None,
        sonnet_pct=None,
        five_hour_reset="1h",
        seven_day_reset="2d",
    )
    rows = [c for c in build_claude_compact_page(near_full)["children"] if c["type"] == "hstack"]
    assert rows[0]["children"][1]["color"] == "accent"  # 92% → red


def test_compact_page_header_is_knockout_title():
    header = build_claude_compact_page(make_usage())["children"][0]
    assert header["value"] == "CLAUDE"
    assert header["color"] == "white"
    assert header["bg"] == "accent"


def test_header_is_red_knockout_title():
    page = build_claude_page(make_usage(), sparkline=None)
    header = page["children"][0]
    assert header["type"] == "text"
    assert header["value"] == "CLAUDE"
    assert header["color"] == "white"
    assert header["bg"] == "accent"


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
