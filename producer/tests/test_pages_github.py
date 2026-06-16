import datetime as dt

from rcink.pages.github import build_github_page
from rcink.sources.github import GRID_CELLS, GitHubActivity


def _all_text(node: dict) -> list[str]:
    out: list[str] = []
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == "text":
            out.append(n.get("value", ""))
        stack.extend(n.get("children", []) or [])
    return out


def _find_nodes(node: dict, typ: str) -> list[dict]:
    out: list[dict] = []
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == typ:
            out.append(n)
        stack.extend(n.get("children", []) or [])
    return out


def _make(
    *,
    today: int = 3,
    week: int = 18,
    streak: int = 5,
    total_14w: int = 142,
) -> GitHubActivity:
    return GitHubActivity(
        user="rocry",
        today_commits=today,
        week_commits=week,
        streak=streak,
        total_14w=total_14w,
        total_year=420,
        daily_counts=[0] * (GRID_CELLS - 4) + [1, 5, 2, 8],
    )


def test_github_page_kpi_strip_shows_today_week_14w():
    page = build_github_page(_make(today=3, week=18, total_14w=142), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    assert "TODAY" in texts and "WEEK" in texts and "14W" in texts
    assert "3" in texts and "18" in texts and "142" in texts


def test_github_page_user_line_shows_streak():
    page = build_github_page(_make(streak=7), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    assert any("@rocry" in t for t in texts)
    assert any("7d streak" in t for t in texts)


def test_github_page_uses_daily_sparkline_instead_of_heatmap_grid():
    page = build_github_page(_make(), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    sparklines = _find_nodes(page, "sparkline")

    assert any("COMMITS / DAY" in t for t in texts)
    assert len(sparklines) == 1
    assert sparklines[0]["values"] == [0] * (GRID_CELLS - 4) + [1, 5, 2, 8]
    assert _find_nodes(page, "grid") == []
