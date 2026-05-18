"""Compact "everything at a glance" page — shown first on boot.

Six dense blocks across two columns: Claude 5h + 7d, Codex primary + weekly,
next-event hero, and an open-todo count. Any source that fails to fetch is
rendered as a dash placeholder so a transient outage doesn't blank the whole
page.
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


def _kpi_block(
    brand: str,
    brand_color: str,
    primary_label: str,
    primary_pct: float | None,
    primary_reset: str,
    secondary_label: str,
    secondary_pct: float | None,
    secondary_reset: str,
) -> dict:
    """One brand's column: title chip, two stacked %+bar rows."""

    def row(lbl: str, pct: float | None, reset: str) -> dict:
        pct_text = f"{int(pct)}%" if pct is not None else "—"
        return vstack(
            gap=2,
            children=[
                hstack(
                    align="center",
                    children=[
                        text(lbl, size="sm", weight="bold"),
                        spacer(),
                        text(reset or "", size="sm"),
                    ],
                ),
                hstack(
                    gap=6,
                    align="center",
                    children=[
                        text(pct_text, size="h2", color=_pct_color(pct), weight="bold"),
                        hbar(pct=pct if pct is not None else 0, color=brand_color, height=12),
                    ],
                ),
            ],
        )

    return vstack(
        gap=6,
        children=[
            text(brand, size="sm", weight="bold", color="white", bg=brand_color, pad=4, align="center"),
            row(primary_label, primary_pct, primary_reset),
            row(secondary_label, secondary_pct, secondary_reset),
        ],
    )


def _next_event_block(cal: TodaysCalendar | None, now: dt.datetime) -> dict:
    if cal is None:
        return vstack(
            gap=4,
            children=[
                text("NEXT EVENT", size="sm", weight="bold", color="white", bg="blue", pad=4, align="center"),
                spacer(size=4),
                text("—", size="h2", weight="bold", align="center"),
            ],
        )
    upcoming = [e for e in cal.events if (e.is_all_day or e.end > now)]
    if not upcoming:
        return vstack(
            gap=4,
            children=[
                text("NEXT EVENT", size="sm", weight="bold", color="white", bg="blue", pad=4, align="center"),
                spacer(size=4),
                text("CLEAR", size="h2", weight="bold", color="green", align="center"),
                text(f"{len(cal.events)} events today", size="sm", align="center"),
            ],
        )
    nxt = upcoming[0]
    when = "ALL" if nxt.is_all_day else nxt.start.strftime("%H:%M")
    if not nxt.is_all_day:
        delta_min = max(0, int((nxt.start - now).total_seconds() // 60))
        if delta_min < 60:
            sub = f"in {delta_min}m"
        else:
            h, m = divmod(delta_min, 60)
            sub = f"in {h}h" if m == 0 else f"in {h}h {m}m"
    else:
        sub = "all day"
    return vstack(
        gap=4,
        children=[
            text("NEXT EVENT", size="sm", weight="bold", color="white", bg="blue", pad=4, align="center"),
            hstack(
                gap=8,
                align="end",
                children=[
                    text(when, size="h2", color="blue", weight="bold"),
                    vstack(
                        gap=2,
                        children=[
                            text(nxt.title, size="sm", weight="bold", max_lines=1),
                            text(sub, size="sm"),
                        ],
                    ),
                ],
            ),
        ],
    )


def _todo_block(rem: TodaysReminders | None) -> dict:
    if rem is None:
        return vstack(
            gap=4,
            children=[
                text("OPEN TODOS", size="sm", weight="bold", color="white", bg="black", pad=4, align="center"),
                spacer(size=4),
                text("—", size="h2", weight="bold", align="center"),
            ],
        )
    count = rem.incomplete_total
    color = "red" if count >= 20 else "yellow" if count >= 5 else "green"
    return vstack(
        gap=4,
        children=[
            text("OPEN TODOS", size="sm", weight="bold", color="white", bg="black", pad=4, align="center"),
            hstack(
                gap=8,
                align="end",
                children=[
                    text(str(count), size="h2", color=color, weight="bold"),
                    vstack(
                        gap=2,
                        children=[
                            text(rem.list_name, size="sm", weight="bold", max_lines=1),
                            text("incomplete", size="sm"),
                        ],
                    ),
                ],
            ),
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
    # Header — date stamp + masthead.
    header = hstack(
        align="center",
        children=[
            text("OVERVIEW", size="h1", weight="bold"),
            spacer(),
            text(now.strftime("%a · %b %d").upper(), size="sm", weight="bold"),
        ],
    )

    claude_block = _kpi_block(
        brand="CLAUDE",
        brand_color="red",
        primary_label="5H",
        primary_pct=claude.five_hour_pct if claude else None,
        primary_reset=claude.five_hour_reset if claude else "",
        secondary_label="7D",
        secondary_pct=claude.seven_day_pct if claude else None,
        secondary_reset=claude.seven_day_reset if claude else "",
    )
    codex_block = _kpi_block(
        brand="CODEX",
        brand_color="green",
        primary_label="5H",
        primary_pct=codex.primary_pct if codex else None,
        primary_reset=codex.primary_reset if codex else "",
        secondary_label="7D",
        secondary_pct=codex.secondary_pct if codex else None,
        secondary_reset=codex.secondary_reset if codex else "",
    )

    return vstack(
        pad=14,
        gap=10,
        children=[
            header,
            divider(color="black", thickness=2),
            # Row 1: Claude | Codex
            hstack(
                gap=10,
                children=[
                    claude_block,
                    codex_block,
                ],
            ),
            divider(color="black", thickness=1),
            # Row 2: Next event | Open todos
            hstack(
                gap=10,
                children=[
                    _next_event_block(calendar, now),
                    _todo_block(reminders),
                ],
            ),
            spacer(),
            divider(color="black", thickness=2),
            hstack(
                align="center",
                gap=8,
                children=[
                    icon("claude", size=12, color="red"),
                    icon("codex", size=12, color="green"),
                    icon("calendar", size=12, color="blue"),
                    icon("todo", size=12, color="black"),
                    spacer(),
                    text("rcpaper · overview", size="sm", weight="bold"),
                ],
            ),
        ],
    )
