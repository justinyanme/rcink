"""Build the Todo page from macOS Reminders.

Mirrors `buildTodo` in the handoff: TODO sticker header, count chip, checkbox
list with priority badges, and a DONE progress bar. Within protocol — uses
unicode checkbox + priority chips (no proposed primitives).
"""

from __future__ import annotations

import re

from rcpaper.layout import divider, hbar, hstack, icon, spacer, text, vstack
from rcpaper.sources.reminders import Reminder, TodaysReminders

_URL_RE = re.compile(r"https?://\S+")


def _clean_title(raw: str, *, limit: int = 36) -> str:
    """Strip URLs and collapse whitespace so the row stays scannable.

    Many reminders are literally just URLs; replace each with a [link] sentinel
    so the row still hints at content. Hard-cap chars (the firmware also
    truncates with an ellipsis, but trimming at the source keeps Axiom events
    small and gives the layout engine accurate row widths).
    """
    cleaned = _URL_RE.sub("[link]", raw)
    cleaned = " ".join(cleaned.split())
    if not cleaned:
        cleaned = "[link]"
    if len(cleaned) > limit:
        cleaned = cleaned[: limit - 1].rstrip() + "…"
    return cleaned


# EventKit priority buckets → palette + label.
#   1..3 = high, 4..6 = medium, 7..9 = low, 0 = none.
def _prio_color(p: int) -> str | None:
    if p == 0:
        return None
    if p <= 3:
        return "red"
    if p <= 6:
        return "yellow"
    return "blue"


def _item_row(item: Reminder) -> dict:
    # The ☐ unicode glyph doesn't render in our bundled fonts; use an ASCII
    # box that the FreeSans / efontCN fonts both have.
    checkbox = text("[ ]", size="body", color="black", weight="bold")
    prio_color = _prio_color(item.priority)
    children: list[dict] = [checkbox]
    if prio_color is not None:
        children.append(text("!", size="sm", color="white", bg=prio_color, pad=3, weight="bold"))
    else:
        # Reserve the priority slot so titles align across rows.
        children.append(spacer(size=10))
    children.append(text(_clean_title(item.title), size="body", weight="bold", max_lines=1))
    return hstack(gap=8, align="center", children=children)


def build_todo_page(rem: TodaysReminders) -> dict:
    total = max(1, rem.incomplete_total)  # avoid div-by-zero
    visible = rem.items[:8]
    count_chip = f"{rem.list_name.upper()} · {len(visible)} / {rem.incomplete_total}"

    children: list[dict] = [
        hstack(
            align="center",
            children=[
                text("TODO", size="h1", color="white", bg="black", pad=6, weight="bold"),
                spacer(),
                text(count_chip, size="sm", weight="bold", bg="yellow", pad=6),
            ],
        ),
        spacer(size=6),
    ]

    if not visible:
        children += [
            spacer(size=20),
            text("ALL CLEAR", size="h1", color="green", weight="bold", align="center"),
            spacer(size=8),
            text("inbox zero", size="body", align="center"),
        ]
    else:
        for item in visible:
            children.append(_item_row(item))

    pct = int(100 * (1 - len(rem.items) / total))
    children += [
        spacer(),
        divider(color="black", thickness=2),
        spacer(size=4),
        hstack(
            gap=6,
            align="center",
            children=[
                text("DONE", size="sm", weight="bold", bg="green", color="white", pad=4),
                hbar(pct=pct, color="green", height=12),
                text(f"{pct}%", size="sm", weight="bold"),
            ],
        ),
        spacer(size=4),
        hstack(
            align="center",
            gap=6,
            children=[
                icon("todo", size=14, color="black"),
                text(f"TODO  ·  {rem.list_name}", size="sm", weight="bold"),
            ],
        ),
    ]

    return vstack(pad=16, gap=8, children=children)
