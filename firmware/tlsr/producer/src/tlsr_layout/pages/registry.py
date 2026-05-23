"""Registered page builders keyed by daemon/CLI page name."""

from collections.abc import Callable

from .calendar import build_page as calendar_page
from .claude import build_page as claude_page
from .codex import build_page as codex_page
from .example import example_page
from .todo import build_page as todo_page

PageBuilder = Callable[[], dict]

PAGES: dict[str, PageBuilder] = {
    "calendar": calendar_page,
    "claude": claude_page,
    "codex": codex_page,
    "example": example_page,
    "todo": todo_page,
}


def page_names() -> list[str]:
    return sorted(PAGES)


def build_page(name: str) -> dict:
    builder = PAGES.get(name)
    if builder is None:
        raise KeyError(f"unknown page {name!r}; known: {page_names()}")
    return builder()
