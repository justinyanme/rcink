import pytest

from tlsr_layout.pages.registry import build_page, page_names


def test_page_names_includes_example():
    assert "example" in page_names()


def test_build_unknown_raises():
    with pytest.raises(KeyError, match="unknown page 'nope'"):
        build_page("nope")


def test_build_example():
    page = build_page("example")
    assert page["type"] == "vstack"
