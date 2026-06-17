"""Build the GitHub activity page — compact contribution edition.

KPI strip (TODAY / WEEK / 14W contributions), streak, and 14-week daily line chart.
"""

from __future__ import annotations

import datetime as dt

from rcink.layout import divider, hstack, icon, spacer, sparkline, text, vstack
from rcink.palette import ColorToken
from rcink.sources.github import GRID_WEEKS, GitHubActivity


def _kpi_cell(label: str, value: str, color: ColorToken, sub: str, *, align: str = "start") -> dict:
    return vstack(
        align=align,
        gap=2,
        children=[
            text(label, size="sm", weight="bold", align=align),
            text(value, size="h1", color=color, weight="bold", align=align),
            text(sub, size="sm", align=align),
        ],
    )


def _avg(values: list[int]) -> str:
    if not values:
        return "0"
    avg = sum(values) / len(values)
    return str(int(avg)) if avg.is_integer() else f"{avg:.1f}"


def build_github_page(activity: GitHubActivity, *, now: dt.datetime) -> dict:
    streak_color: ColorToken = "accent" if activity.streak >= 7 else "warning" if activity.streak >= 3 else "success"
    streak_text = f"{activity.streak}d streak" if activity.streak > 0 else "no streak"

    children: list[dict] = [
        hstack(
            align="center",
            children=[
                text("GITHUB", size="h1", weight="bold"),
                spacer(),
                text(now.strftime("%a · %b %d").upper(), size="sm", weight="bold"),
            ],
        ),
        divider(color="black", thickness=2),
        spacer(size=2),
        hstack(
            align="center",
            gap=6,
            children=[
                icon("link", size=14, color="black"),
                text(f"@{activity.user}", size="sm", weight="bold"),
                spacer(),
                text(streak_text, size="sm", weight="bold", color=streak_color),
            ],
        ),
        spacer(size=6),
        hstack(
            align="center",
            gap=6,
            children=[
                _kpi_cell("TODAY", str(activity.today_contributions), "success", "CONTRIBS", align="start"),
                spacer(),
                _kpi_cell("WEEK", str(activity.week_contributions), "info", "CONTRIBS", align="center"),
                spacer(),
                _kpi_cell("14W", str(activity.total_14w), "accent", "TOTAL", align="end"),
            ],
        ),
        spacer(size=6),
        divider(color="black", thickness=1),
        spacer(size=4),
        hstack(
            align="center",
            children=[
                text(f"CONTRIBS / DAY · {GRID_WEEKS}W", size="sm", weight="bold"),
                spacer(),
                text(f"MAX {max(activity.daily_counts or [0])}", size="sm", weight="bold"),
                text(f"AVG {_avg(activity.daily_counts)}", size="sm"),
            ],
        ),
        spacer(size=4),
        sparkline(
            activity.daily_counts,
            color="success",
            height=96,
        ),
    ]

    return vstack(pad=14, gap=6, children=children)
