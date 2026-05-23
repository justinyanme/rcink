"""Compact "everything at a glance" page — shown first on boot.

Single-column vertical layout: each block is a full-width row that the
layout engine already handles cleanly (text + flex-hbar). Multi-column
nested hstacks collapse to natural width on this engine, which would
leave the right ~2/3 of the page empty — so we just stack everything.
"""

from __future__ import annotations

import datetime as dt

from rcpaper.layout import divider, hbar, hstack, icon, spacer, text, vstack
from rcpaper.pages._common import relative_short, wrap_visual
from rcpaper.palette import ColorToken
from rcpaper.sources.calendar import TodaysCalendar
from rcpaper.sources.claude import ClaudeUsage
from rcpaper.sources.codex import CodexUsage
from rcpaper.sources.github import GitHubActivity
from rcpaper.sources.quotes import Quote
from rcpaper.sources.reminders import TodaysReminders

# Visual-cell budget for one wrapped quote line at h2-bold on a 540px panel.
_QUOTE_LINE_LIMIT = 30
_QUOTE_MAX_LINES = 3


def _quote_block(q: Quote | None) -> list[dict]:
    """Return 0..N text nodes for the multi-line quote block.

    Empty list when there's nothing to show — header omits the block entirely.
    """
    if q is None:
        return []
    joined = " ".join(line.strip() for line in q.lines if line.strip())
    if not joined:
        return []
    lines = wrap_visual(joined, line_limit=_QUOTE_LINE_LIMIT, max_lines=_QUOTE_MAX_LINES)
    out: list[dict] = []
    for i, line in enumerate(lines):
        prefix = '"' if i == 0 else ""
        suffix = '"' if i == len(lines) - 1 else ""
        out.append(text(f"{prefix}{line}{suffix}", size="h2", weight="bold"))
    return out


def _github_row(gh: GitHubActivity | None) -> dict:
    """One row: [github icon] [GITHUB chip] [N today] [N week] [top:repo] [streak]."""
    if gh is None:
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("github", size=16, color="black"),
                text("GITHUB", size="sm", weight="bold", color="white", bg="black", pad=4),
                text("—", size="body", weight="bold"),
                spacer(),
            ],
        )
    today_color: ColorToken = "green" if gh.today_commits > 0 else "black"
    week_color: ColorToken = "green" if gh.week_commits > 0 else "black"
    streak_color: ColorToken = "red" if gh.streak >= 7 else "yellow" if gh.streak >= 3 else "green"
    children: list[dict] = [
        icon("github", size=16, color="black"),
        text("GITHUB", size="sm", weight="bold", color="white", bg="black", pad=4),
        text(str(gh.today_commits), size="h2", color=today_color, weight="bold"),
        text("today", size="sm", weight="bold"),
        text(str(gh.week_commits), size="h2", color=week_color, weight="bold"),
        text("week", size="sm", weight="bold"),
    ]
    if gh.repos_week:
        children += [
            text("top:", size="sm"),
            text(gh.repos_week[0].name, size="body", weight="bold"),
        ]
    children += [
        spacer(),
        text(f"{gh.streak}d", size="sm", weight="bold", color=streak_color),
    ]
    return hstack(gap=8, align="center", children=children)


def _pct_color(pct: float | None) -> ColorToken:
    if pct is None:
        return "black"
    if pct >= 80:
        return "red"
    if pct >= 50:
        return "yellow"
    return "green"


def _usage_row(
    brand: str,
    brand_color: ColorToken,
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
    today_upcoming = [e for e in cal.events if (e.is_all_day or e.end > now)]
    if today_upcoming:
        nxt = today_upcoming[0]
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
                text(title, size="body", weight="bold"),
                spacer(),
                text(sub, size="sm"),
            ],
        )

    if cal.upcoming:
        nxt = cal.upcoming[0]
        dow = nxt.start.strftime("%a").upper()
        when = "ALL" if nxt.is_all_day else nxt.start.strftime("%H:%M")
        title = nxt.title
        if len(title) > 22:
            title = title[:21] + "…"
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("calendar", size=16, color="blue"),
                text("SOON", size="sm", weight="bold", color="white", bg="blue", pad=4),
                text(f"{dow} {when}", size="body", color="blue", weight="bold"),
                text(title, size="body", weight="bold"),
                spacer(),
            ],
        )

    return hstack(
        gap=8,
        align="center",
        children=[
            icon("calendar", size=16, color="blue"),
            text("NEXT", size="sm", weight="bold", color="white", bg="blue", pad=4),
            text("CLEAR", size="body", weight="bold", color="green"),
            spacer(),
            text("nothing this week", size="sm"),
        ],
    )


_TODO_PEEK_LIMIT = 24


def _todos_row(rem: TodaysReminders | None, *, now: dt.datetime) -> dict:
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
    count_color: ColorToken = "red" if count >= 20 else "yellow" if count >= 5 else "green"
    children: list[dict] = [
        icon("todo", size=16, color="black"),
        text("TODO", size="sm", weight="bold", color="white", bg="black", pad=4),
        text(str(count), size="h2", color=count_color, weight="bold"),
    ]
    if not rem.items:
        children += [
            text("ALL CLEAR", size="body", weight="bold", color="green"),
            spacer(),
            text(rem.list_name, size="sm"),
        ]
        return hstack(gap=8, align="center", children=children)
    top = rem.items[0]
    title_lines = wrap_visual(top.title, line_limit=_TODO_PEEK_LIMIT, max_lines=1)
    title_text = title_lines[0] if title_lines else top.title
    # Omit closing quote when title was truncated so the node ends with `…`.
    title_display = f'"{title_text}' if title_text.endswith("…") else f'"{title_text}"'
    rel = relative_short(top.updated, now=now)
    children += [
        text(title_display, size="body", weight="bold"),
        spacer(),
        text(rel, size="sm", weight="bold", color="blue"),
    ]
    return hstack(gap=8, align="center", children=children)


def build_overview_page(
    *,
    claude: ClaudeUsage | None,
    codex: CodexUsage | None,
    calendar: TodaysCalendar | None,
    reminders: TodaysReminders | None,
    github: GitHubActivity | None = None,
    quote: Quote | None = None,
    now: dt.datetime,
) -> dict:
    date_label = text(now.strftime("%a · %b %d").upper(), size="sm", weight="bold")
    header = hstack(
        align="center",
        children=[spacer(), date_label],
    )
    quote_nodes = _quote_block(quote)

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
    github_row = _github_row(github)

    page_children: list[dict] = [header]
    if quote_nodes:
        page_children.append(spacer(size=2))
        page_children.extend(quote_nodes)
    page_children += [
        divider(color="black", thickness=2),
        spacer(size=4),
        claude_row,
        codex_row,
        github_row,
        spacer(size=4),
        divider(color="black", thickness=1),
        spacer(size=4),
        _next_event_row(calendar, now),
        _todos_row(reminders, now=now),
        spacer(),
    ]
    return vstack(pad=16, gap=10, children=page_children)
