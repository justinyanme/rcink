"""Build the Todo page from macOS Reminders.

TODO sticker header, count chip, checkbox list with priority badges,
DONE progress bar at the foot.
"""

from __future__ import annotations

import datetime as dt
import re
from urllib.parse import urlparse

from rcpaper.layout import divider, hbar, hstack, icon, spacer, text, vstack
from rcpaper.sources.reminders import Reminder, TodaysReminders

_URL_RE = re.compile(r"https?://\S+")
VISIBLE_ITEMS = 10


def _extract_hosts(raw: str) -> tuple[list[str], str]:
    """Pull out hostnames + return (hosts, residual_text).

    The residual is whatever non-URL words remained after stripping; the
    caller decides whether to render it before or after the hostnames.
    """
    hosts: list[str] = []

    def _sub(m: re.Match[str]) -> str:
        try:
            h = urlparse(m.group(0)).hostname or ""
        except ValueError:
            h = ""
        if h.startswith("www."):
            h = h[4:]
        if h:
            hosts.append(h)
        return ""  # remove URL from residual

    residual = _URL_RE.sub(_sub, raw)
    residual = " ".join(residual.split())
    return hosts, residual


def _trim(s: str, *, limit: int) -> str:
    return s if len(s) <= limit else s[: limit - 1].rstrip() + "…"


def _relative_short(when: dt.datetime | None, *, now: dt.datetime) -> str:
    if when is None:
        return ""
    delta = now - when
    seconds = max(0, int(delta.total_seconds()))
    if seconds < 60:
        return "now"
    minutes = seconds // 60
    if minutes < 60:
        return f"{minutes}m"
    hours = minutes // 60
    if hours < 24:
        return f"{hours}h"
    days = hours // 24
    if days < 7:
        return f"{days}d"
    weeks = days // 7
    if weeks < 5:
        return f"{weeks}w"
    months = max(1, days // 30)
    return f"{months}mo"


def render_item_children(item: Reminder, *, limit: int = 24) -> list[dict]:
    """Build the right-hand side of a todo row.

    URL-heavy reminders become `[icon] hostname` (+ residual text if any),
    so a row that's literally `https://github.com/foo/bar` reads as
    `🔗 github.com` rather than a wall of URL chars that the layout would
    have to truncate.
    """
    hosts, residual = _extract_hosts(item.title)
    nodes: list[dict] = []
    if hosts:
        # link icon + first hostname (matters most for scannability); show
        # the second hostname inline if there's room.
        nodes.append(icon("link", size=16, color="blue"))
        head = hosts[0]
        if len(hosts) > 1 and len(head) + 2 + len(hosts[1]) <= limit:
            head = f"{head} · {hosts[1]}"
        nodes.append(text(head, size="body", weight="bold"))
        if residual:
            tail = _trim(residual, limit=limit - len(head) - 2)
            if tail:
                nodes.append(text(f"— {tail}", size="sm"))
    else:
        nodes.append(text(_trim(residual or item.title, limit=limit), size="body", weight="bold"))
    return nodes


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


def _item_row(item: Reminder, *, now: dt.datetime) -> dict:
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
    children.extend(render_item_children(item))
    relative = _relative_short(item.updated, now=now)
    if relative:
        children.append(spacer())
        children.append(text(relative, size="sm", weight="bold", color="blue"))
    return hstack(gap=8, align="center", children=children)


def build_todo_page(rem: TodaysReminders, *, now: dt.datetime | None = None) -> dict:
    now = now or dt.datetime.now().astimezone()
    total = max(1, rem.incomplete_total)  # avoid div-by-zero
    visible = rem.items[:VISIBLE_ITEMS]
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
            children.append(_item_row(item, now=now))

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
    ]

    return vstack(pad=16, gap=4, children=children)
