"""Compact "everything at a glance" page — shown first on boot.

Single-column vertical layout: each block is a full-width row that the
layout engine already handles cleanly (text + flex-hbar). Multi-column
nested hstacks collapse to natural width on this engine, which would
leave the right ~2/3 of the page empty — so we just stack everything.
"""

from __future__ import annotations

import datetime as dt
from typing import Literal

from rcink.layout import divider, hbar, hstack, spacer, text, vstack
from rcink.pages._common import relative_short, trim, wrap_visual, wrap_visual_all
from rcink.palette import ColorToken
from rcink.sources.calendar import TodaysCalendar
from rcink.sources.claude import ClaudeUsage
from rcink.sources.codex import CodexUsage
from rcink.sources.github import GitHubActivity
from rcink.sources.quotes import Quote
from rcink.sources.reminders import TodaysReminders
from rcink.sources.weather import Weather

type WeatherIssue = Literal["config", "fetch", "unavailable"]


_QUOTE_FITS: tuple[tuple[str, int, int], ...] = (
    # size, visual cells per line, max lines in the overview header block
    ("h2", 30, 3),
    ("body", 44, 4),
    ("sm", 64, 5),
)
_LABEL_SIZE = "sm"
_ROW_CONTENT_DY = 2
_PRECIP_THRESHOLD = 20


def _quote_block(q: Quote | None) -> list[dict]:
    """Return 0..N text nodes for the multi-line quote block.

    Empty list when there's nothing to show — header omits the block entirely.
    """
    if q is None:
        return []
    joined = " ".join(line.strip() for line in q.lines if line.strip())
    if not joined:
        return []
    selected_size = "sm"
    for candidate_size, line_limit, max_lines in _QUOTE_FITS:
        lines = wrap_visual_all(joined, line_limit=line_limit)
        if len(lines) <= max_lines:
            selected_size = candidate_size
            break
    else:
        raise ValueError(f"overview quote is too long to fit: {joined!r}")
    out: list[dict] = []
    for i, line in enumerate(lines):
        prefix = '"' if i == 0 else ""
        suffix = '"' if i == len(lines) - 1 else ""
        out.append(text(f"{prefix}{line}{suffix}", size=selected_size, weight="bold"))
    return out


def _github_row(gh: GitHubActivity | None) -> dict:
    """One row: [GITHUB chip] [N today] [N week] [top:repo] [streak]."""
    if gh is None:
        return hstack(
            gap=8,
            align="center",
            children=[
                text("GITHUB", size=_LABEL_SIZE, weight="bold", color="white", bg="black", pad=4),
                text("—", size="sm", weight="bold"),
                spacer(),
            ],
        )
    today_color: ColorToken = "success" if gh.today_contributions > 0 else "black"
    week_color: ColorToken = "success" if gh.week_contributions > 0 else "black"
    streak_color: ColorToken = "accent" if gh.streak >= 7 else "warning" if gh.streak >= 3 else "success"
    children: list[dict] = [
        text("GITHUB", size=_LABEL_SIZE, weight="bold", color="white", bg="black", pad=4),
        text(str(gh.today_contributions), size="sm", color=today_color, weight="bold"),
        text("today", size="sm", weight="bold"),
        text(str(gh.week_contributions), size="sm", color=week_color, weight="bold"),
        text("week", size="sm", weight="bold"),
    ]
    if gh.repos_week:
        children += [
            text("top:", size="sm"),
            text(gh.repos_week[0].name, size="sm", weight="bold"),
        ]
    children += [
        spacer(),
        text(f"{gh.streak}d", size="sm", weight="bold", color=streak_color),
    ]
    return hstack(gap=8, align="center", children=children)


def _weather_label(code: int) -> tuple[str, ColorToken]:
    if code in {0, 1}:
        return "SUN", "success"
    if code in {2, 3, 45, 48}:
        return "CLOUD", "info"
    if 51 <= code <= 67 or 80 <= code <= 82:
        return "RAIN", "warning"
    if 71 <= code <= 77 or 85 <= code <= 86:
        return "SNOW", "info"
    if 95 <= code <= 99:
        return "STORM", "accent"
    return "WX", "black"


def _precip_label(code: int) -> str:
    if 95 <= code <= 99:
        return "STORM"
    if 71 <= code <= 77 or 85 <= code <= 86:
        return "SNOW"
    return "RAIN"


def _weather_row(weather: Weather | None, *, issue: WeatherIssue | None = None) -> dict:
    if weather is None:
        status, detail, color = {
            "config": ("CONFIG", "SET LAT LON", "warning"),
            "fetch": ("ERROR", "FETCH FAILED", "accent"),
            "unavailable": ("UNAVAILABLE", "CHECK LOG", "warning"),
            None: ("UNAVAILABLE", "CHECK LOG", "warning"),
        }[issue]
        return hstack(
            gap=8,
            align="center",
            children=[
                text("WEATHER", size=_LABEL_SIZE, weight="bold", color="white", bg="info", pad=4),
                text(status, size="sm", weight="bold", color=color),
                spacer(),
                text(detail, size="sm", weight="bold", color=color),
            ],
        )
    label, color = _weather_label(weather.code)
    children = [
        text(weather.location, size=_LABEL_SIZE, weight="bold", color="white", bg="info", pad=4),
        text(f"{round(weather.temp)}{weather.unit}", size="sm", weight="bold", color=color),
        text(label, size="sm", weight="bold", color=color),
        text(f"{round(weather.low)}~{round(weather.high)}{weather.unit}", size="sm"),
        spacer(),
    ]
    if weather.precip_probability is not None and weather.precip_probability >= _PRECIP_THRESHOLD:
        children.append(
            text(f"{_precip_label(weather.code)} {weather.precip_probability}%", size="sm", weight="bold", color=color)
        )
    return hstack(
        gap=8,
        align="center",
        children=children,
    )


def _pct_color(pct: float | None) -> ColorToken:
    if pct is None:
        return "black"
    if pct >= 80:
        return "accent"
    if pct >= 50:
        return "warning"
    return "success"


def _compact_time(s: str) -> str:
    return s.replace(" ", "")


def _short_window_label(minutes: int | None) -> str:
    if minutes is None:
        return "WINDOW"
    if minutes % (24 * 60) == 0:
        return f"{minutes // (24 * 60)}D"
    if minutes % 60 == 0:
        return f"{minutes // 60}H"
    return f"{minutes}M"


def _usage_row(
    brand: str,
    brand_color: ColorToken,
    primary_pct: float | None,
    primary_label: str,
    primary_reset: str,
    secondary_pct: float | None,
    secondary_label: str | None,
) -> dict:
    """One full-width row: brand, primary bar, optional secondary pct, reset."""
    pct_text = "—" if primary_pct is None else f"{int(primary_pct)}%"
    children = [
        text(brand, size=_LABEL_SIZE, weight="bold", color="white", bg=brand_color, pad=4),
        text(primary_label, size="body", weight="bold"),
        text(pct_text, size="body", color=_pct_color(primary_pct), weight="bold"),
        hbar(pct=primary_pct if primary_pct is not None else 0, color=brand_color, height=14),
    ]
    if secondary_label is not None:
        secondary_text = f"{int(secondary_pct)}%" if secondary_pct is not None else "—"
        children.append(
            text(f"{secondary_label} {secondary_text}", size="body", weight="bold", color=_pct_color(secondary_pct))
        )
    children.append(text(_compact_time(primary_reset), size="body"))
    return hstack(gap=8, align="center", children=children)


def _next_event_row(cal: TodaysCalendar | None, now: dt.datetime) -> dict:
    if cal is None:
        return hstack(
            gap=8,
            align="center",
            children=[
                text("NEXT", size=_LABEL_SIZE, weight="bold", color="white", bg="info", pad=4),
                text("—", size="body", weight="bold", pad=4),
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
        title = trim(nxt.title, limit=24)
        return hstack(
            gap=8,
            align="center",
            children=[
                text("NEXT", size=_LABEL_SIZE, weight="bold", color="white", bg="info", pad=4),
                text(when, size="body", color="info", weight="bold", pad=4),
                text(title, size="body", weight="bold", pad=4, dy=_ROW_CONTENT_DY),
                spacer(),
                text(sub, size="body", pad=4),
            ],
        )

    if cal.upcoming:
        nxt = cal.upcoming[0]
        dow = nxt.start.strftime("%a").upper()
        when = "ALL" if nxt.is_all_day else nxt.start.strftime("%H:%M")
        title = trim(nxt.title, limit=22)
        return hstack(
            gap=8,
            align="center",
            children=[
                text("SOON", size=_LABEL_SIZE, weight="bold", color="white", bg="info", pad=4),
                text(f"{dow} {when}", size="body", color="info", weight="bold", pad=4),
                text(title, size="body", weight="bold", pad=4, dy=_ROW_CONTENT_DY),
                spacer(),
            ],
        )

    return hstack(
        gap=8,
        align="center",
        children=[
            text("NEXT", size=_LABEL_SIZE, weight="bold", color="white", bg="info", pad=4),
            text("CLEAR", size="body", weight="bold", color="success", pad=4),
            spacer(),
            text("nothing this week", size="body", pad=4),
        ],
    )


_TODO_PEEK_LIMIT = 24


def _todos_row(rem: TodaysReminders | None, *, now: dt.datetime) -> dict:
    if rem is None:
        return hstack(
            gap=8,
            align="center",
            children=[
                text("TODO", size=_LABEL_SIZE, weight="bold", color="white", bg="black", pad=4),
                text("—", size="body", weight="bold", pad=4),
                spacer(),
            ],
        )
    count = rem.incomplete_total
    count_color: ColorToken = "accent" if count >= 20 else "warning" if count >= 5 else "success"
    children: list[dict] = [
        text("TODO", size=_LABEL_SIZE, weight="bold", color="white", bg="black", pad=4),
        text(str(count), size="body", color=count_color, weight="bold", pad=4),
    ]
    if not rem.items:
        children += [
            text("ALL CLEAR", size="body", weight="bold", color="success", pad=4, dy=_ROW_CONTENT_DY),
            spacer(),
            text(rem.list_name, size="body", pad=4),
        ]
        return hstack(gap=8, align="center", children=children)
    top = rem.items[0]
    title_lines = wrap_visual(top.title, line_limit=_TODO_PEEK_LIMIT, max_lines=1)
    title_text = title_lines[0] if title_lines else top.title
    # Omit closing quote when title was truncated so the node ends with `…`.
    title_display = f'"{title_text}' if title_text.endswith("…") else f'"{title_text}"'
    rel = relative_short(top.updated, now=now)
    children += [
        text(title_display, size="body", weight="bold", pad=4, dy=_ROW_CONTENT_DY),
        spacer(),
        text(rel, size="body", weight="bold", color="info", pad=4),
    ]
    return hstack(gap=8, align="center", children=children)


def build_overview_page(
    *,
    claude: ClaudeUsage | None,
    codex: CodexUsage | None,
    calendar: TodaysCalendar | None,
    reminders: TodaysReminders | None,
    now: dt.datetime,
    weather: Weather | None = None,
    weather_issue: WeatherIssue | None = None,
    github: GitHubActivity | None = None,
    quote: Quote | None = None,
) -> dict:
    date_label = text(now.strftime("%a · %b %d").upper(), size="sm", weight="bold")
    header = hstack(
        align="center",
        children=[spacer(), date_label],
    )
    quote_nodes = _quote_block(quote)

    claude_row = _usage_row(
        brand="CLAUDE",
        brand_color="accent",
        primary_pct=claude.seven_day_pct if claude else None,
        primary_label="7D",
        primary_reset=claude.seven_day_reset if claude else "",
        secondary_pct=None,
        secondary_label=None,
    )
    codex_row = _usage_row(
        brand="CODEX",
        brand_color="success",
        primary_pct=codex.primary_pct if codex else None,
        primary_label=_short_window_label(codex.primary_window_min) if codex else "WINDOW",
        primary_reset=codex.primary_reset if codex else "",
        secondary_pct=codex.secondary_pct if codex else None,
        secondary_label=(
            _short_window_label(codex.secondary_window_min) if codex and codex.secondary_pct is not None else None
        ),
    )
    github_row = _github_row(github)
    weather_row = _weather_row(weather, issue=weather_issue)

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
        weather_row,
        spacer(size=4),
        divider(color="black", thickness=1),
        spacer(size=4),
        _next_event_row(calendar, now),
        _todos_row(reminders, now=now),
        spacer(),
    ]
    return vstack(pad=16, gap=10, children=page_children)
