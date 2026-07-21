import datetime as dt

from rcink.pages._common import relative_short, trim, visual_width, wrap_visual


def test_wrap_visual_short_string_single_line():
    assert wrap_visual("hello world", line_limit=20, max_lines=3) == ["hello world"]


def test_wrap_visual_breaks_on_space():
    out = wrap_visual("hello world foo bar baz", line_limit=11, max_lines=3)
    assert out == ["hello world", "foo bar baz"]


def test_wrap_visual_caps_at_max_lines_with_ellipsis():
    out = wrap_visual(
        "alpha bravo charlie delta echo foxtrot golf hotel",
        line_limit=12,
        max_lines=2,
    )
    assert len(out) == 2
    assert out[-1].endswith("…")


def test_wrap_visual_cjk_counts_two_cells():
    # 6 wide chars = 12 cells; with line_limit=8 we should wrap after 4 chars.
    out = wrap_visual("一二三四五六", line_limit=8, max_lines=2)
    assert len(out) == 2
    assert visual_width(out[0]) <= 8
    assert visual_width(out[1]) <= 8


def test_wrap_visual_hard_break_when_no_spaces():
    # No whitespace at all — fall back to character break.
    out = wrap_visual("abcdefghijkl", line_limit=4, max_lines=3)
    assert out == ["abcd", "efgh", "ijkl"]


def test_wrap_visual_max_lines_one_truncates():
    out = wrap_visual("alpha bravo charlie", line_limit=10, max_lines=1)
    assert len(out) == 1
    assert out[0].endswith("…")


def test_trim_counts_cjk_as_two_cells():
    out = trim("会议abcdef", limit=8)
    assert out == "会议abc…"
    assert visual_width(out) <= 8


def test_relative_short_minutes():
    now = dt.datetime(2026, 5, 23, 12, 0, tzinfo=dt.UTC)
    assert relative_short(now - dt.timedelta(minutes=5), now=now) == "5m"


def test_relative_short_hours():
    now = dt.datetime(2026, 5, 23, 12, 0, tzinfo=dt.UTC)
    assert relative_short(now - dt.timedelta(hours=3), now=now) == "3h"


def test_relative_short_none():
    now = dt.datetime(2026, 5, 23, 12, 0, tzinfo=dt.UTC)
    assert relative_short(None, now=now) == ""


def test_relative_short_now():
    now = dt.datetime(2026, 5, 23, 12, 0, tzinfo=dt.UTC)
    assert relative_short(now, now=now) == "now"
