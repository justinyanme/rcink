"""Build the GitHub activity page.

Header with today's date, @user line, three KPI cells (today / streak / PRs),
and a 14-week contribution heatmap rendered via the `grid` primitive.
"""

from __future__ import annotations

import datetime as dt

from rcpaper.layout import divider, grid, hstack, icon, spacer, text, vstack
from rcpaper.palette import ColorToken
from rcpaper.sources.github import GRID_WEEKS, GitHubActivity


def _kpi_cell(label: str, value: str, color: ColorToken, sub: str) -> dict:
    return vstack(
        gap=2,
        children=[
            text(label, size="sm", weight="bold"),
            text(value, size="h1", color=color, weight="bold"),
            text(sub, size="sm"),
        ],
    )


def build_github_page(activity: GitHubActivity, *, now: dt.datetime) -> dict:
    return vstack(
        pad=14,
        gap=6,
        children=[
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
                ],
            ),
            spacer(size=6),
            hstack(
                gap=12,
                children=[
                    _kpi_cell("TODAY", str(activity.today_commits), "green", "COMMITS"),
                    _kpi_cell("STREAK", f"{activity.streak}d", "red", "DAILY"),
                    _kpi_cell(
                        "PRs",
                        f"{activity.pr_open}/{activity.pr_review}",
                        "blue",
                        "OPEN / REVIEW",
                    ),
                ],
            ),
            spacer(size=8),
            divider(color="black", thickness=1),
            spacer(size=4),
            hstack(
                align="center",
                children=[
                    text(f"CONTRIBUTIONS · {GRID_WEEKS}W", size="sm", weight="bold"),
                    spacer(),
                    text(
                        f"{activity.total_14w} TOTAL",
                        size="sm",
                        weight="bold",
                        color="green",
                    ),
                ],
            ),
            spacer(size=4),
            grid(
                cols=GRID_WEEKS,
                rows=7,
                gap=3,
                color="green",
                values=activity.contrib,
                width=360,
                height=168,
            ),
        ],
    )
