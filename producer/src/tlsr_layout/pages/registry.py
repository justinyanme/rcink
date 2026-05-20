"""Registered page builders keyed by daemon/CLI page name."""

from collections.abc import Callable

from .example import example_page

PageBuilder = Callable[[], dict]

PAGES: dict[str, PageBuilder] = {
    "example": example_page,
}


def page_names() -> list[str]:
    return sorted(PAGES)


def build_page(name: str) -> dict:
    builder = PAGES.get(name)
    if builder is None:
        raise KeyError(f"unknown page {name!r}; known: {page_names()}")
    return builder()
