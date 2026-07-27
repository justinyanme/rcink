"""rcink's KP3 card cache and frame rendering.

The wire grammar itself is `rcink.kp3`'s (and its own repo's tests'); what is
checked here is rcink's card shape on top of it — row rendering, which line
carries what, the cache round-trip, and the deck the pusher emits.
"""

import datetime as dt
import json

import pytest

from rcink import kp3
from rcink.keypoint import (
    CARD_ORDER,
    Card,
    Row,
    fmt_countdown,
    frames_to_push,
    fresh_cards,
    load_cards,
    render_frame,
    write_card,
)

NOW = dt.datetime(2026, 6, 10, 12, 34, 56).astimezone()
NOW_TS = int(NOW.timestamp())


def make_card(**over) -> Card:
    base = dict(
        id="claude",
        icon="CLAUDE",
        title="CLAUDE",
        led_hint=0,
        rows=[Row("5H", "76%"), Row("WK", "88%"), Row("RST", until=NOW_TS + 65 * 60), Row("OP", "18%")],
        data_ts=NOW_TS - 600,  # fetched 10 min ago
    )
    base.update(over)
    return Card(**base)


# --- format helpers ---


@pytest.mark.parametrize(
    ("seconds", "expected"),
    [
        (-5, "NOW"),
        (0, "NOW"),
        (59, "0m"),
        (59 * 60, "59m"),
        (60 * 60, "1h"),
        (83 * 60, "1h23m"),
        (9 * 3600 + 59 * 60, "9h59m"),
        (10 * 3600, "10h"),
        (47 * 3600, "47h"),
        (48 * 3600, "2d"),
        (9 * 86400, "9d"),
    ],
)
def test_fmt_countdown(seconds, expected):
    assert fmt_countdown(seconds) == expected


# --- frame rendering ---


def test_render_frame_layout():
    frame = render_frame(make_card(), now=NOW, generation=0xA0, index=0, total=1)
    fields = frame.decode("ascii").split("|")
    assert fields[0] == "KP3"
    assert fields[1] == "A0"  # GEN (deck transaction)
    assert fields[2] == "0"  # IDX (0-based page)
    assert fields[3] == "1"  # TOTAL (deck size)
    assert fields[4] == "CLAUDE"  # icon
    assert fields[5] == "0"  # LED hint
    assert fields[6] == "CLAUDE   "  # L1 title, left-anchored
    assert fields[7] == "5H    76%"  # L2 = rows[0] key metric
    assert fields[8] == (NOW - dt.timedelta(minutes=10)).strftime("%H:%M")  # L3 = data_ts
    assert fields[9] == "WK    88%"
    assert fields[10] == "RST  1h5m"  # countdown computed at render time, not fetch time
    assert fields[11] == "OP    18%"
    assert len(frame) <= kp3.FRAME_MAX


def test_render_frame_fills_the_contract_line_width():
    """The drift this producer had for a while: labels padded one column short."""
    fields = render_frame(make_card(), now=NOW, generation=0xA0, index=0, total=1).decode("ascii").split("|")
    kv_lines = [fields[6], fields[7], fields[9], fields[10], fields[11]]  # title + kv rows, not the HH:MM stamp
    assert {len(line) for line in kv_lines} == {kp3.LINE_MAX}


def test_render_frame_carries_idx_total():
    fields = render_frame(make_card(), now=NOW, generation=0xA0, index=2, total=5).decode("ascii").split("|")
    assert (fields[1], fields[2], fields[3], fields[4], fields[5]) == ("A0", "2", "5", "CLAUDE", "0")
    assert fields[6] == "CLAUDE   "  # LED hint now precedes L1


def test_render_frame_carries_led_hint():
    fields = render_frame(make_card(led_hint=3), now=NOW, generation=0xA0, index=0, total=1).decode("ascii").split("|")
    assert fields[5] == "3"


def test_render_frame_rejects_bad_led_hint():
    with pytest.raises(ValueError, match="LED hint"):
        render_frame(make_card(led_hint=len(kp3.LED_CODES)), now=NOW, generation=0xA0, index=0, total=1)


def test_render_frame_rejects_bad_generation():
    with pytest.raises(ValueError, match="generation"):
        render_frame(make_card(), now=NOW, generation=-1, index=0, total=1)
    with pytest.raises(ValueError, match="generation"):
        render_frame(make_card(), now=NOW, generation=16**kp3.GENERATION_FIELD_MAX, index=0, total=1)


def test_render_frame_rejects_bad_page_args():
    with pytest.raises(ValueError, match="IDX"):
        render_frame(make_card(), now=NOW, generation=0xA0, index=3, total=3)  # idx must be < total
    with pytest.raises(ValueError, match="TOTAL"):
        render_frame(make_card(), now=NOW, generation=0xA0, index=0, total=0)  # total must be >= 1
    with pytest.raises(ValueError, match="TOTAL"):
        render_frame(make_card(), now=NOW, generation=0xA0, index=0, total=kp3.PAGE_MAX + 1)


def test_render_frame_l3_is_data_ts_not_now():
    card = make_card(data_ts=NOW_TS - 3600)
    frame = render_frame(card, now=NOW, generation=0xA0, index=0, total=1)
    l3 = frame.decode("ascii").split("|")[8]
    assert l3 == (NOW - dt.timedelta(hours=1)).strftime("%H:%M")


def test_render_frame_pads_missing_rows_with_empty_lines():
    card = make_card(rows=[Row("EV", "3")])
    fields = render_frame(card, now=NOW, generation=0xA0, index=0, total=1).decode("ascii").split("|")
    assert len(fields) == 12  # KP3 + GEN + IDX + TOTAL + ICON + LED + 6 lines
    assert fields[9] == fields[10] == fields[11] == ""


def test_render_frame_supports_fifth_raw_row_without_timestamp():
    card = make_card(
        rows=[
            Row("5H", until=NOW_TS + 90 * 60),
            Row(bar=76),
            Row("7D", until=NOW_TS + 4 * 86400),
            Row(bar=88),
            Row(text="FB 41%"),
        ],
        show_timestamp=False,
    )

    fields = render_frame(card, now=NOW, generation=0xA0, index=0, total=1).decode("ascii").split("|")

    assert fields[11] == "FB 41%"


def test_render_frame_rejects_sixth_row_without_timestamp():
    with pytest.raises(ValueError, match="expected 1..5 rows"):
        render_frame(
            make_card(rows=[Row("A", "1")] * 6, show_timestamp=False),
            now=NOW,
            generation=0xA0,
            index=0,
            total=1,
        )


def test_render_frame_rejects_bad_icon():
    with pytest.raises(ValueError, match="icon"):
        render_frame(make_card(icon="WEATHER"), now=NOW, generation=0xA0, index=0, total=1)


def test_render_frame_rejects_unsupported_chars():
    with pytest.raises(ValueError):
        render_frame(make_card(title="日历"), now=NOW, generation=0xA0, index=0, total=1)
    with pytest.raises(ValueError):
        render_frame(make_card(rows=[Row("A|B", "1")]), now=NOW, generation=0xA0, index=0, total=1)


def test_render_frame_rejects_fifth_row_when_timestamp_is_shown():
    with pytest.raises(ValueError, match="expected 1..4 rows"):
        render_frame(make_card(rows=[Row("A", "1")] * 5), now=NOW, generation=0xA0, index=0, total=1)


# --- card cache ---


@pytest.fixture
def cache(tmp_path, monkeypatch):
    monkeypatch.setenv("RCINK_KEYPOINT_CACHE", str(tmp_path))
    return tmp_path


def test_write_card_roundtrip(cache):
    card = make_card()
    write_card(card)
    (loaded,) = load_cards()
    assert loaded == card


def test_write_card_roundtrip_bar_rows(cache):
    card = make_card(rows=[Row("5H", until=NOW_TS + 90 * 60), Row(bar=76), Row("7D", "88%"), Row(bar=88)])
    write_card(card)
    (loaded,) = load_cards()
    assert loaded == card
    assert loaded.rows[1].bar == 76
    assert loaded.rows[3].bar == 88


def test_write_card_is_one_file_per_card(cache):
    write_card(make_card())
    write_card(make_card(id="codex", icon="CODEX", title="CODEX"))
    assert {p.name for p in cache.iterdir()} == {"claude.json", "codex.json"}


def test_write_card_fails_fast_on_invalid_content(cache):
    with pytest.raises(ValueError):
        write_card(make_card(title="TOOLONGTITLE"))
    assert not list(cache.iterdir())


def test_load_cards_orders_by_rotation(cache):
    # Known cards follow CARD_ORDER; unknown ids sort after them alphabetically.
    for id_ in ("todo", "claude", "github"):
        write_card(make_card(id=id_, icon="NONE"))
    assert [c.id for c in load_cards()] == ["claude", "todo", "github"]
    assert CARD_ORDER == ("claude", "codex", "weather", "calendar", "todo")


def test_load_cards_fails_fast_on_corrupt_file(cache):
    write_card(make_card())
    (cache / "junk.json").write_text("{not json")
    with pytest.raises(ValueError, match="invalid KEYPOINT card cache file"):
        load_cards()


def test_load_cards_fails_fast_on_old_schema(cache):
    write_card(make_card())
    data = json.loads((cache / "claude.json").read_text())
    del data["led_hint"]
    (cache / "claude.json").write_text(json.dumps(data))
    with pytest.raises(ValueError, match="led_hint"):
        load_cards()


def test_fresh_cards_drops_stale():
    fresh = make_card()
    stale = make_card(id="todo", data_ts=NOW_TS - 7 * 3600)
    assert fresh_cards([fresh, stale], now=NOW, max_age_s=6 * 3600) == [fresh]


def test_card_json_is_plain_and_compact(cache):
    """On-disk shape stays greppable: top-level scalars + rows list."""
    write_card(make_card())
    data = json.loads((cache / "claude.json").read_text())
    assert data["icon"] == "CLAUDE"
    assert data["led_hint"] == 0
    assert data["rows"][2] == {"label": "RST", "until": NOW_TS + 65 * 60}
    assert data["rows"][0] == {"label": "5H", "value": "76%"}


# --- frames_to_push (load + freshness + render glue the pusher consumes) ---


def test_frames_to_push_renders_fresh_cards_in_order(cache):
    write_card(make_card(id="todo", icon="NONE", rows=[Row("DUE", "2")]))
    write_card(make_card(id="claude"))
    frames = frames_to_push(now=NOW, max_age_s=6 * 3600)
    icons = [f.decode("ascii").split("|")[4] for f in frames]  # ICON after GEN/IDX/TOTAL
    assert icons == ["CLAUDE", "NONE"]  # rotation order, not write order


def test_frames_to_push_stamps_index_and_total(cache):
    write_card(make_card(id="claude"))
    write_card(make_card(id="codex", icon="CODEX", title="CODEX"))
    frames = frames_to_push(now=NOW, max_age_s=6 * 3600)
    parsed = [f.decode("ascii").split("|") for f in frames]
    # Whole deck pushed under one generation: each card stamped with page index and deck total.
    assert len({p[1] for p in parsed}) == 1
    assert [(p[2], p[3], p[4], p[5]) for p in parsed] == [
        ("0", "2", "CLAUDE", "0"),
        ("1", "2", "CODEX", "0"),
    ]


def test_frames_to_push_drops_stale(cache):
    write_card(make_card(id="claude"))
    write_card(make_card(id="todo", icon="NONE", rows=[Row("DUE", "1")], data_ts=NOW_TS - 7 * 3600))
    frames = frames_to_push(now=NOW, max_age_s=6 * 3600)
    assert [f.decode("ascii").split("|")[4] for f in frames] == ["CLAUDE"]
