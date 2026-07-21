from rcink.pages.codex import build_codex_page
from rcink.sources.codex import CodexUsage


def make_usage():
    return CodexUsage(
        primary_pct=42.0,
        primary_reset="2h 14m",
        primary_window_min=300,
        secondary_pct=68.0,
        secondary_reset="3d 5h",
        secondary_window_min=10080,
        credits_dollars=18.42,
        plan="Plus",
    )


def test_root_is_vstack_with_green_header():
    page = build_codex_page(make_usage(), sparkline=None)
    assert page["type"] == "vstack"
    header = page["children"][0]
    assert header["value"] == "CODEX"
    assert header["bg"] == "success"


def test_window_labels_match_window_minutes():
    page = build_codex_page(make_usage(), sparkline=None)
    text_values = [
        c["children"][0]["children"][0]["value"]
        for c in page["children"]
        if c["type"] == "vstack" and c.get("children") and c["children"][0].get("type") == "hstack"
    ]
    # primary_window_min=300 → "5H WINDOW", secondary=10080 → "WEEKLY"
    assert "5H WINDOW" in text_values
    assert "WEEKLY" in text_values


def test_credits_ribbon_present():
    page = build_codex_page(make_usage(), sparkline=None)
    # Look for the yellow text node carrying credits info
    credits_nodes = [c for c in page["children"] if c["type"] == "text" and c.get("bg") == "warning"]
    assert credits_nodes
    assert "$18.42" in credits_nodes[0]["value"]


def test_credits_omitted_when_none():
    usage = CodexUsage(
        primary_pct=42.0,
        primary_reset="2h",
        primary_window_min=300,
        secondary_pct=None,
        secondary_reset="",
        secondary_window_min=None,
        credits_dollars=None,
    )
    page = build_codex_page(usage, sparkline=None)
    assert not any(c["type"] == "text" and c.get("bg") == "warning" for c in page["children"])


def test_single_weekly_window_omits_secondary_kpi():
    usage = CodexUsage(
        primary_pct=12.0,
        primary_reset="4d 18h",
        primary_window_min=10080,
        secondary_pct=None,
        secondary_reset="",
        secondary_window_min=None,
        credits_dollars=None,
    )

    page = build_codex_page(usage, sparkline=None)
    kpi_blocks = [child for child in page["children"] if child["type"] == "vstack"]

    assert len(kpi_blocks) == 1
    assert kpi_blocks[0]["children"][0]["children"][0]["value"] == "WEEKLY"
