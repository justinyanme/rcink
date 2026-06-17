import pytest
from rcink.devices_profile import C3_X4, S3, T_AMOLED, TLSR, get_profile


def test_get_profile_known():
    assert get_profile("s3") is S3
    assert get_profile("c3-x4") is C3_X4
    assert get_profile("tlsr") is TLSR
    assert get_profile("t-amoled") is T_AMOLED


def test_get_profile_unknown_raises():
    with pytest.raises(KeyError):
        get_profile("nope")


def test_t_amoled_sm_size_matches_dsl_spec():
    assert T_AMOLED.sizes["Sm"] == 8
    assert T_AMOLED.sizes["sm"] == 8


@pytest.mark.parametrize(
    "token,expect",
    [
        ("black", "#000000"),
        ("white", "#FFFFFF"),
        ("red", "#FF0000"),
        ("yellow", "#FFFF00"),
        ("green", "#00FF00"),
        ("blue", "#0000FF"),
        ("accent", "#FF0000"),
        ("warning", "#FFFF00"),
        ("success", "#00FF00"),
        ("info", "#0000FF"),
        ("muted", "#000000"),
    ],
)
def test_s3_resolves_full_palette(token, expect):
    assert S3.resolve_color(token) == expect


def test_c3_x4_only_white_stays_white():
    assert C3_X4.resolve_color("white") == "#FFFFFF"
    for t in ("black", "red", "yellow", "green", "blue", "accent", "warning", "success", "info", "muted"):
        assert C3_X4.resolve_color(t) == "#000000"


@pytest.mark.parametrize(
    "token,expect",
    [
        ("black", "#000000"),
        ("white", "#FFFFFF"),
        ("red", "#FF0000"),
        ("blue", "#FF0000"),
        ("yellow", "#000000"),
        ("green", "#000000"),
        ("accent", "#FF0000"),
        ("warning", "#FF0000"),
        ("success", "#000000"),
        ("info", "#000000"),
        ("muted", "#000000"),
    ],
)
def test_tlsr_bwr_reduction(token, expect):
    assert TLSR.resolve_color(token) == expect


@pytest.mark.parametrize(
    "token,expect",
    [
        ("black", "#000000"),
        ("white", "#FFFFFF"),
        ("red", "#FF3333"),
        ("yellow", "#FFC533"),
        ("green", "#30D158"),
        ("blue", "#3B82F6"),
        ("accent", "#FF3333"),
        ("warning", "#FFC533"),
        ("success", "#30D158"),
        ("info", "#3B82F6"),
        ("muted", "#000000"),
    ],
)
def test_t_amoled_resolves_rgb_palette(token, expect):
    assert T_AMOLED.resolve_color(token) == expect
