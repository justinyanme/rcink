"""`keypoint-push --demo` and `--frames`: both must hand the firmware a deck it commits.

The oracle is kp4 (derived from the firmware C): `kp4.read_deck` refuses any
deck the firmware would never commit.
"""

import datetime as dt

import kp4
import pytest
from rcink.keypoint import render_deck
from rcink.keypoint_demo import demo_cards
from scripts.keypoint_push import _deck

NOW = dt.datetime(2026, 11, 1, 12, 0).astimezone()


def test_demo_deck_is_one_committable_deck_with_every_producer_card(tmp_path):
    deck = render_deck(demo_cards(now=NOW), now=NOW)
    path = tmp_path / "demo.kp4"
    path.write_text("\n".join(frame.decode("ascii") for frame in deck))

    frames = kp4.read_deck(path)

    assert [kp4.parse(frame).lines[0].strip() for frame in frames] == ["CLAUDE", "CODEX", "WEATHER", "CAL", "TODO"]


def test_render_deck_refuses_more_pages_than_the_firmware_holds():
    cards = demo_cards(now=NOW) * 2  # 10 > PAGE_MAX 8
    with pytest.raises(ValueError, match="PAGE_MAX"):
        render_deck(cards, now=NOW)


def test_frames_file_is_pushed_verbatim(tmp_path):
    path = tmp_path / "deck.kp4"
    path.write_text(
        "# hand-written\nKP4|B0|0|2|CLAUDE|0|001000|CLAUDE   |7D     4d|[041]|||\nKP4|B0|1|2|SUN|0|000000|SUNNY|||||\n"
    )

    assert _deck(frames_file=path, demo=False, max_age_s=60) == [
        b"KP4|B0|0|2|CLAUDE|0|001000|CLAUDE   |7D     4d|[041]|||",
        b"KP4|B0|1|2|SUN|0|000000|SUNNY|||||",
    ]


def test_frames_file_that_would_never_commit_fails_before_any_bluetooth(tmp_path):
    path = tmp_path / "deck.kp4"
    path.write_text("KP4|B0|0|2|SUN|0|000000|SUNNY|||||\n")  # page 1 of 2 never comes

    with pytest.raises(kp4.FrameError):
        _deck(frames_file=path, demo=False, max_age_s=60)
