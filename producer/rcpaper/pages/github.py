"""Build the GitHub activity page — velocity edition.

KPI strip (TODAY / WEEK / 14W commits), top repos this week with bar widths,
weighted-language legend, 14-week contribution heatmap.
"""

from __future__ import annotations

import datetime as dt

from rcpaper.layout import divider, grid, hbar, hstack, icon, spacer, text, vstack
from rcpaper.palette import ColorToken
from rcpaper.sources.github import GRID_WEEKS, GitHubActivity, LangWeek, RepoWeek

# Cycle for language legend colors (kept in sync with the rest of the producer).
_LANG_COLORS: tuple[ColorToken, ...] = ("green", "blue", "red", "yellow", "black")


def _kpi_cell(label: str, value: str, color: ColorToken, sub: str) -> dict:
    return vstack(
        gap=2,
        children=[
            text(label, size="sm", weight="bold"),
            text(value, size="h1", color=color, weight="bold"),
            text(sub, size="sm"),
        ],
    )


def _repos_section(repos: list[RepoWeek]) -> list[dict]:
    if not repos:
        return []
    top = repos[0].commits or 1
    rows: list[dict] = [
        text("REPOS · 7D", size="sm", weight="bold"),
        spacer(size=2),
    ]
    for r in repos:
        pct = max(2.0, min(100.0, (r.commits / top) * 100.0))
        rows.append(
            hstack(
                gap=8,
                align="center",
                children=[
                    text(r.name, size="body", weight="bold"),
                    hbar(pct=pct, color="green", height=10),
                    text(str(r.commits), size="sm", weight="bold"),
                ],
            )
        )
    return rows


def _langs_section(langs: list[LangWeek]) -> list[dict]:
    if not langs:
        return []
    total_pct = sum(lang.pct for lang in langs) or 1
    legend_children: list[dict] = []
    for i, lang in enumerate(langs):
        color = _LANG_COLORS[i % len(_LANG_COLORS)]
        if i > 0:
            legend_children.append(text("·", size="sm"))
        legend_children.append(text(f"{lang.name} {lang.pct}", size="sm", weight="bold", color=color))
    return [
        text("LANGUAGES · 7D", size="sm", weight="bold"),
        spacer(size=2),
        hbar(pct=min(100.0, total_pct), color="green", height=10),
        spacer(size=2),
        hstack(gap=6, align="center", children=legend_children),
    ]


def build_github_page(activity: GitHubActivity, *, now: dt.datetime) -> dict:
    streak_color: ColorToken = "red" if activity.streak >= 7 else "yellow" if activity.streak >= 3 else "green"
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
            gap=12,
            children=[
                _kpi_cell("TODAY", str(activity.today_commits), "green", "COMMITS"),
                _kpi_cell("WEEK", str(activity.week_commits), "blue", "COMMITS"),
                _kpi_cell("14W", str(activity.total_14w), "red", "TOTAL"),
            ],
        ),
        spacer(size=6),
        divider(color="black", thickness=1),
        spacer(size=4),
    ]

    children += _repos_section(activity.repos_week)
    if activity.repos_week:
        children.append(spacer(size=6))
    children += _langs_section(activity.langs_week)
    if activity.langs_week:
        children.append(spacer(size=6))

    children += [
        divider(color="black", thickness=1),
        spacer(size=4),
        hstack(
            align="center",
            children=[
                text(f"CONTRIBUTIONS · {GRID_WEEKS}W", size="sm", weight="bold"),
                spacer(),
                text(f"{activity.total_14w} TOTAL", size="sm", weight="bold", color="green"),
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
    ]

    return vstack(pad=14, gap=6, children=children)
