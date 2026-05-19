"""Compact "everything at a glance" page — shown first on boot.

Single-column vertical layout: each block is a full-width row that the
layout engine already handles cleanly (text + flex-hbar). Multi-column
nested hstacks collapse to natural width on this engine, which would
leave the right ~2/3 of the page empty — so we just stack everything.
"""

from __future__ import annotations

import datetime as dt

from rcpaper.layout import divider, hbar, hstack, icon, spacer, text, vstack
from rcpaper.sources.calendar import TodaysCalendar
from rcpaper.sources.claude import ClaudeUsage
from rcpaper.sources.codex import CodexUsage
from rcpaper.sources.reminders import TodaysReminders


def _pct_color(pct: float | None) -> str:
    if pct is None:
        return "black"
    if pct >= 80:
        return "red"
    if pct >= 50:
        return "yellow"
    return "green"


def _usage_row(
    brand: str,
    brand_color: str,
    icon_name: str,
    primary_pct: float | None,
    primary_label: str,
    primary_reset: str,
    secondary_pct: float | None,
    secondary_label: str,
) -> dict:
    """One full-width row: [BRAND chip] [primary % + flex hbar] [7D pct]."""
    pct_text = "—" if primary_pct is None else f"{int(primary_pct)}%"
    secondary_text = f"{int(secondary_pct)}%" if secondary_pct is not None else "—"

    return hstack(
        gap=8,
        align="center",
        children=[
            icon(icon_name, size=16, color=brand_color),
            text(brand, size="sm", weight="bold", color="white", bg=brand_color, pad=4),
            text(primary_label, size="sm", weight="bold"),
            text(pct_text, size="h2", color=_pct_color(primary_pct), weight="bold"),
            hbar(pct=primary_pct if primary_pct is not None else 0, color=brand_color, height=14),
            text(f"{secondary_label} {secondary_text}", size="sm", weight="bold", color=_pct_color(secondary_pct)),
            text(primary_reset or "", size="sm"),
        ],
    )


def _next_event_row(cal: TodaysCalendar | None, now: dt.datetime) -> dict:
    if cal is None:
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("calendar", size=16, color="blue"),
                text("NEXT", size="sm", weight="bold", color="white", bg="blue", pad=4),
                text("—", size="body", weight="bold"),
                spacer(),
            ],
        )
    upcoming = [e for e in cal.events if (e.is_all_day or e.end > now)]
    if not upcoming:
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("calendar", size=16, color="blue"),
                text("NEXT", size="sm", weight="bold", color="white", bg="blue", pad=4),
                text("CLEAR", size="body", weight="bold", color="green"),
                spacer(),
                text(f"{len(cal.events)} today", size="sm"),
            ],
        )
    nxt = upcoming[0]
    when = "ALL" if nxt.is_all_day else nxt.start.strftime("%H:%M")
    if nxt.is_all_day:
        sub = "all day"
    else:
        delta_min = max(0, int((nxt.start - now).total_seconds() // 60))
        if delta_min < 60:
            sub = f"in {delta_min}m"
        else:
            h, m = divmod(delta_min, 60)
            sub = f"in {h}h" if m == 0 else f"in {h}h {m}m"
    # Truncate event title at the source so the row stays single-line.
    title = nxt.title
    if len(title) > 24:
        title = title[:23] + "…"
    return hstack(
        gap=8,
        align="center",
        children=[
            icon("calendar", size=16, color="blue"),
            text("NEXT", size="sm", weight="bold", color="white", bg="blue", pad=4),
            text(when, size="body", color="blue", weight="bold"),
            text(title, size="body", weight="bold", max_lines=1),
            spacer(),
            text(sub, size="sm"),
        ],
    )


def _todos_row(rem: TodaysReminders | None) -> dict:
    if rem is None:
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("todo", size=16, color="black"),
                text("TODO", size="sm", weight="bold", color="white", bg="black", pad=4),
                text("—", size="body", weight="bold"),
                spacer(),
            ],
        )
    count = rem.incomplete_total
    color = "red" if count >= 20 else "yellow" if count >= 5 else "green"
    return hstack(
        gap=8,
        align="center",
        children=[
            icon("todo", size=16, color="black"),
            text("TODO", size="sm", weight="bold", color="white", bg="black", pad=4),
            text(str(count), size="h2", color=color, weight="bold"),
            text("OPEN", size="sm", weight="bold"),
            spacer(),
            text(rem.list_name, size="sm"),
        ],
    )


def build_overview_page(
    *,
    claude: ClaudeUsage | None,
    codex: CodexUsage | None,
    calendar: TodaysCalendar | None,
    reminders: TodaysReminders | None,
    now: dt.datetime,
) -> dict:
    header = hstack(
        align="center",
        children=[
            text("OVERVIEW", size="h1", weight="bold"),
            spacer(),
            text(now.strftime("%a · %b %d").upper(), size="sm", weight="bold"),
        ],
    )

    claude_row = _usage_row(
        brand="CLAUDE",
        brand_color="red",
        icon_name="claude",
        primary_pct=claude.five_hour_pct if claude else None,
        primary_label="5H",
        primary_reset=claude.five_hour_reset if claude else "",
        secondary_pct=claude.seven_day_pct if claude else None,
        secondary_label="7D",
    )
    codex_row = _usage_row(
        brand="CODEX",
        brand_color="green",
        icon_name="codex",
        primary_pct=codex.primary_pct if codex else None,
        primary_label="5H",
        primary_reset=codex.primary_reset if codex else "",
        secondary_pct=codex.secondary_pct if codex else None,
        secondary_label="7D",
    )

    return vstack(
        pad=16,
        gap=12,
        children=[
            header,
            divider(color="black", thickness=2),
            spacer(size=4),
            claude_row,
            codex_row,
            spacer(size=4),
            divider(color="black", thickness=1),
            spacer(size=4),
            _next_event_row(calendar, now),
            _todos_row(reminders),
            spacer(),
        ],
    )
