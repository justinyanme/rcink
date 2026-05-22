"""Build the Todo page from macOS Reminders.

TODO sticker header, count chip, checkbox list with priority badges,
DONE progress bar at the foot.
"""

from __future__ import annotations

import datetime as dt
import re
import unicodedata
from urllib.parse import urlparse

from rcpaper.layout import divider, hbar, hstack, icon, spacer, text, vstack
from rcpaper.palette import ColorToken
from rcpaper.sources.reminders import Reminder, TodaysReminders

_URL_RE = re.compile(r"https?://\S+")
VISIBLE_ITEMS = 10
# Visual-cell budget for a single body-line title. The title column gets
# ~430 px after the marker + priority + relative-time chrome; VT323 16pt is
# ~12 px/cell → ~32 cells. We stay a touch under so descenders + the wide
# efontCN glyphs (which are ~16 px each, i.e. ~2 cells) don't kiss the edge.
LINE_LIMIT = 28


def _cell_width(c: str) -> int:
    """1 for narrow / 2 for wide. Matches the VT323-vs-efontCN width ratio."""
    return 2 if unicodedata.east_asian_width(c) in ("W", "F") else 1


def _visual_width(s: str) -> int:
    return sum(_cell_width(c) for c in s)


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
    """Truncate `s` to ≤ `limit` visual cells, appending `…` if cut."""
    if _visual_width(s) <= limit:
        return s
    # Reserve 1 cell for the ellipsis.
    out = ""
    width = 0
    for c in s:
        w = _cell_width(c)
        if width + w + 1 > limit:
            break
        out += c
        width += w
    return out.rstrip() + "…"


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


def _split_two_lines(body: str, *, line_limit: int) -> list[str]:
    """Wrap `body` into at most 2 lines of ~`line_limit` visual cells each.

    Visual cells, not Python chars: a CJK glyph counts as 2 so a row of
    Chinese characters wraps at the same physical width as a row of Latin
    letters. Prefer a space boundary; for spaceless CJK, fall back to a
    hard character break. The second line gets an ellipsis if it overflows.
    """
    if _visual_width(body) <= line_limit:
        return [body]

    # Walk char-by-char, tracking visual width + the last whitespace index
    # whose run still fits inside the budget.
    width = 0
    last_space = -1
    hard_break = len(body)
    for i, c in enumerate(body):
        w = _cell_width(c)
        if width + w > line_limit:
            hard_break = i
            break
        width += w
        if c.isspace():
            last_space = i

    cut = last_space if last_space > 0 else hard_break
    line1 = body[:cut].rstrip()
    rest = body[cut:].lstrip()
    return [line1, _trim(rest, limit=line_limit)]


def render_title_node(item: Reminder, *, line_limit: int = LINE_LIMIT) -> dict:
    """Build the title cell — a single node to slot into the row hstack.

    URL-heavy reminders become `[icon] hostname` (+ residual text if any) on
    one line, so a row that's literally `https://github.com/foo/bar` reads
    as `🔗 github.com`. Plain titles expand to a 2-line vstack when they
    overflow the single-line budget.
    """
    hosts, residual = _extract_hosts(item.title)
    if hosts:
        # link icon + first hostname (matters most for scannability); show
        # the second hostname inline if there's room.
        head = hosts[0]
        if len(hosts) > 1 and _visual_width(head) + 3 + _visual_width(hosts[1]) <= line_limit:
            head = f"{head} · {hosts[1]}"
        url_row = hstack(
            gap=6,
            align="center",
            children=[
                icon("link", size=16, color="blue"),
                text(head, size="body", weight="bold"),
            ],
        )
        if not residual:
            return url_row
        # If hostname + residual still fits on one line, keep it inline (using
        # an ASCII separator — VT323 has no em-dash glyph, so "—" renders as
        # a missing-glyph box). Otherwise drop the residual onto line 2.
        # +3 covers the link icon's cell budget and the inter-node gap.
        inline_budget = line_limit - _visual_width(head) - 3
        if _visual_width(residual) + 2 <= inline_budget:
            url_row["children"].append(text(f"- {residual}", size="sm"))
            return url_row
        return vstack(
            gap=2,
            children=[
                url_row,
                text(_trim(residual, limit=line_limit), size="sm"),
            ],
        )

    body = residual or item.title
    lines = _split_two_lines(body, line_limit=line_limit)
    if len(lines) == 1:
        return text(lines[0], size="body", weight="bold")
    return vstack(
        gap=2,
        children=[
            text(lines[0], size="body", weight="bold"),
            text(lines[1], size="sm"),
        ],
    )


# EventKit priority buckets → palette + label.
#   1..3 = high, 4..6 = medium, 7..9 = low, 0 = none.
def _prio_color(p: int) -> ColorToken | None:
    if p == 0:
        return None
    if p <= 3:
        return "red"
    if p <= 6:
        return "yellow"
    return "blue"


def _item_row(item: Reminder, *, now: dt.datetime) -> dict:
    # All reminders we render are unchecked by definition (incomplete list),
    # so a compact "todo" glyph reads better than a `[ ]` ASCII box at body
    # font size. Size 14 keeps the marker visually lighter than the title.
    checkbox = icon("todo", size=14, color="black")
    prio_color = _prio_color(item.priority)
    children: list[dict] = [checkbox]
    if prio_color is not None:
        children.append(text("!", size="sm", color="white", bg=prio_color, pad=3, weight="bold"))
    else:
        # Reserve the priority slot so titles align across rows.
        children.append(spacer(size=10))
    children.append(render_title_node(item))
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
