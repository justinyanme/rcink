import pytest

from tlsr_layout.palette import COLORS, is_valid_color, validate_color


def test_six_colors_defined():
    assert COLORS == {"black", "white", "red", "yellow", "green", "blue"}


def test_is_valid_color_true():
    for c in ("black", "white", "red", "yellow", "green", "blue"):
        assert is_valid_color(c) is True


def test_is_valid_color_false():
    assert is_valid_color("purple") is False
    assert is_valid_color("BLACK") is False  # case-sensitive
    assert is_valid_color("") is False
    assert is_valid_color(None) is False  # type: ignore[arg-type]


def test_validate_color_passes_through():
    assert validate_color("red") == "red"


def test_validate_color_raises_on_unknown():
    with pytest.raises(ValueError, match="unknown color 'purple'"):
        validate_color("purple")


def test_validate_color_allows_none_when_optional():
    assert validate_color(None, allow_none=True) is None


def test_validate_color_rejects_none_when_required():
    with pytest.raises(ValueError, match="color is required"):
        validate_color(None)
