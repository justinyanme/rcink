import datetime as dt

from rcink.pages.quote import build_quote_page
from rcink.sources.quotes import Quote


def _quote():
    return Quote(lines=("BUILD", "PROTOCOLS,", "NOT", "PRODUCTS."), attrib="— ALAN KAY")


def test_root_is_vstack():
    page = build_quote_page(_quote(), now=dt.datetime(2026, 5, 18, 14, 32))
    assert page["type"] == "vstack"
    assert page["pad"] == 24


def test_masthead_carries_formatted_date():
    page = build_quote_page(_quote(), now=dt.datetime(2026, 5, 18, 14, 32))
    masthead = page["children"][0]
    assert masthead["type"] == "hstack"
    # Last child of masthead is the date, all caps.
    date_node = masthead["children"][-1]
    assert date_node["type"] == "text"
    # 2026-05-18 is a Monday → "MON · MAY 18".
    assert date_node["value"] == "MON · MAY 18"


def test_body_renders_each_line_as_h1():
    page = build_quote_page(_quote(), now=dt.datetime(2026, 5, 18))
    body_texts = [c for c in page["children"] if c.get("type") == "text" and c.get("size") == "h1"]
    # 1 opening quote glyph + 4 body lines.
    assert len(body_texts) == 5
    assert body_texts[0]["value"] == '"'
    assert [n["value"] for n in body_texts[1:]] == ["BUILD", "PROTOCOLS,", "NOT", "PRODUCTS."]


def test_body_color_cycle_matches_design():
    page = build_quote_page(_quote(), now=dt.datetime(2026, 5, 18))
    body_texts = [c for c in page["children"] if c.get("type") == "text" and c.get("size") == "h1"]
    # Opening glyph is red; body cycles black/blue/black/red.
    assert body_texts[0].get("color") == "red"
    # Line 1: black (no color = default black ink).
    assert body_texts[1].get("color") is None
    assert body_texts[2].get("color") == "blue"
    assert body_texts[3].get("color") is None
    assert body_texts[4].get("color") == "red"


def test_short_quote_does_not_pad_extra_lines():
    short = Quote(lines=("LESS IS MORE.",), attrib="— MIES VAN DER ROHE")
    page = build_quote_page(short, now=dt.datetime(2026, 5, 18))
    body_texts = [c for c in page["children"] if c.get("type") == "text" and c.get("size") == "h1"]
    # 1 quote glyph + exactly 1 body line.
    assert len(body_texts) == 2
    assert body_texts[1]["value"] == "LESS IS MORE."


def test_attribution_is_rendered_flush_right():
    page = build_quote_page(_quote(), now=dt.datetime(2026, 5, 18))
    # Find the hstack containing the attribution (align=end).
    attrib_rows = [c for c in page["children"] if c.get("type") == "hstack" and c.get("align") == "end"]
    assert len(attrib_rows) == 1
    last_child = attrib_rows[0]["children"][-1]
    assert last_child["value"] == "— ALAN KAY"


def test_lines_beyond_four_are_truncated_not_overflowed():
    # A defensive guard: if someone curates a 6-line quote, only 4 render.
    long = Quote(lines=("A", "B", "C", "D", "E", "F"), attrib="— ANON")
    page = build_quote_page(long, now=dt.datetime(2026, 5, 18))
    body_lines = [c for c in page["children"] if c.get("type") == "text" and c.get("size") == "h1"]
    # 1 opening glyph + 4 body lines (E, F dropped).
    assert [n["value"] for n in body_lines] == ['"', "A", "B", "C", "D"]
