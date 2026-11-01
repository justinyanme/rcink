from rcink.devices_profile import EPD397
from rcink.pages.claude import build_claude_compact_page, build_claude_page
from rcink.preview_png import render_to_png
from rcink.sources.claude import ClaudeUsage, ScopedLimit


def _count_nodes(node: dict) -> int:
    return 1 + sum(_count_nodes(c) for c in node.get("children", []))


def make_usage():
    return ClaudeUsage(
        seven_day_pct=68.0,
        scoped_limits=[ScopedLimit(label="FABLE", pct=41.0)],
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


def test_compact_page_shows_the_weekly_window():
    """The window is a row: [label][bar][percent], with label/percent as plain
    text (legible black-on-white) rather than draw_hbar's white-on-fill labels."""
    rows = [c for c in build_claude_compact_page(make_usage())["children"] if c["type"] == "hstack"]
    assert len(rows) == 1
    assert [c["type"] for c in rows[0]["children"]] == ["text", "hbar", "text"]
    assert rows[0]["children"][0]["value"] == "7D"
    assert rows[0]["children"][2]["value"] == "68%"


def test_compact_bar_turns_red_only_when_window_nearly_full():
    rows = [c for c in build_claude_compact_page(make_usage()).get("children", []) if c["type"] == "hstack"]
    assert rows[0]["children"][1]["color"] == "black"  # 68% → black
    near_full = ClaudeUsage(
        seven_day_pct=92.0,
        scoped_limits=[],
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


def test_sparkline_remains_visible_on_epd397():
    page = build_claude_page(make_usage(), sparkline=[1, 2, 3, 4, 5, 6, 7])
    image = render_to_png(page, EPD397)
    lower_middle = image.crop(
        (
            EPD397.width // 3,
            EPD397.height // 2,
            EPD397.width * 2 // 3,
            EPD397.height,
        )
    )

    assert any(pixel != (255, 255, 255) for pixel in lower_middle.get_flattened_data())


def test_scoped_limit_renders_as_bar_row():
    page = build_claude_page(make_usage(), sparkline=None)
    text_values = [
        node.get("value") for child in page["children"] for node in child.get("children", []) if node["type"] == "text"
    ]
    scoped_rows = [
        child
        for child in page["children"]
        if child["type"] == "hstack" and any(node.get("value") == "FABLE" for node in child["children"])
    ]

    assert "FABLE" in text_values
    assert len(scoped_rows) == 1
    assert [node["type"] for node in scoped_rows[0]["children"]] == ["text", "hbar", "text"]
    assert scoped_rows[0]["children"][1] == {
        "type": "hbar",
        "pct": 41.0,
        "color": "success",
        "height": 18,
    }
    assert scoped_rows[0]["children"][2]["value"] == "41%"


def test_scoped_limit_bar_turns_red_at_80_percent():
    usage = make_usage()
    usage.scoped_limits = [ScopedLimit(label="FABLE", pct=80.0)]
    page = build_claude_page(usage, sparkline=None)
    bars = [
        node
        for child in page["children"]
        for node in child.get("children", [])
        if node["type"] == "hbar" and node["pct"] == 80.0
    ]

    assert bars[0]["color"] == "accent"


def test_missing_scoped_limits_skips_section():
    usage = ClaudeUsage(
        seven_day_pct=68.0,
        scoped_limits=[],
        seven_day_reset="3d",
    )
    page = build_claude_page(usage, sparkline=None)
    # Per-model row should be omitted entirely
    text_values = [c.get("value") for c in page["children"] if c["type"] == "text"]
    assert "BY MODEL · 7D" not in text_values


def test_compact_page_explicitly_ignores_scoped_limits():
    page = build_claude_compact_page(make_usage())
    assert "FABLE" not in {
        node.get("value") for child in page["children"] for node in child.get("children", []) if node["type"] == "text"
    }
