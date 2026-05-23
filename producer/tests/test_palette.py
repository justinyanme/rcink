"""Palette validation — DSL v1 accepts concrete + semantic color tokens."""

import pytest
from rcink.layout import text
from rcink.palette import (
    ALL_COLOR_TOKENS,
    E6_COLOR_TOKENS,
    SEMANTIC_COLOR_TOKENS,
    normalize_color,
)


@pytest.mark.parametrize("token", E6_COLOR_TOKENS)
def test_normalize_concrete_token_passes_through(token):
    assert normalize_color(token) == token


@pytest.mark.parametrize("token", SEMANTIC_COLOR_TOKENS)
def test_normalize_semantic_token_passes_through(token):
    assert normalize_color(token) == token


def test_normalize_none_is_none():
    assert normalize_color(None) is None


def test_normalize_unknown_token_raises():
    with pytest.raises(ValueError, match="DSL color tokens"):
        normalize_color("magenta")


def test_all_color_tokens_is_union():
    assert set(ALL_COLOR_TOKENS) == set(E6_COLOR_TOKENS) | set(SEMANTIC_COLOR_TOKENS)


@pytest.mark.parametrize("token", SEMANTIC_COLOR_TOKENS)
def test_text_accepts_semantic_color(token):
    # Smoke: builders must not reject semantic tokens.
    node = text("hello", color=token)
    assert node["color"] == token
