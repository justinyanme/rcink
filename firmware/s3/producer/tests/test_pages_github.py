import datetime as dt

from rcpaper.pages.github import build_github_page
from rcpaper.sources.github import GRID_CELLS, GitHubActivity, LangWeek, RepoWeek


def _all_text(node: dict) -> list[str]:
    out: list[str] = []
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == "text":
            out.append(n.get("value", ""))
        stack.extend(n.get("children", []) or [])
    return out


def _make(
    *,
    today: int = 3,
    week: int = 18,
    streak: int = 5,
    total_14w: int = 142,
    repos: list[RepoWeek] | None = None,
    langs: list[LangWeek] | None = None,
) -> GitHubActivity:
    return GitHubActivity(
        user="rocry",
        today_commits=today,
        week_commits=week,
        streak=streak,
        total_14w=total_14w,
        total_year=420,
        contrib=[0] * GRID_CELLS,
        repos_week=repos or [],
        langs_week=langs or [],
    )


def test_github_page_kpi_strip_shows_today_week_14w():
    page = build_github_page(_make(today=3, week=18, total_14w=142), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    assert "TODAY" in texts and "WEEK" in texts and "14W" in texts
    assert "3" in texts and "18" in texts and "142" in texts


def test_github_page_repos_section_renders_when_present():
    repos = [
        RepoWeek(name="rcpapercolor", commits=12),
        RepoWeek(name="miniomi", commits=5),
    ]
    page = build_github_page(_make(repos=repos), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    assert any(t.startswith("REPOS") for t in texts)
    assert "rcpapercolor" in texts
    assert "miniomi" in texts


def test_github_page_repos_section_omitted_when_empty():
    page = build_github_page(_make(repos=[]), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    assert not any(t.startswith("REPOS") for t in texts)


def test_github_page_languages_section_omitted_when_empty():
    page = build_github_page(_make(langs=[]), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    assert not any(t.startswith("LANGUAGES") for t in texts)


def test_github_page_languages_section_renders_legend():
    langs = [LangWeek(name="Go", pct=60), LangWeek(name="Python", pct=40)]
    page = build_github_page(_make(langs=langs), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    assert any(t.startswith("LANGUAGES") for t in texts)
    legend = " · ".join(texts)
    assert "Go 60" in legend
    assert "Python 40" in legend


def test_github_page_user_line_shows_streak():
    page = build_github_page(_make(streak=7), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    assert any("@rocry" in t for t in texts)
    assert any("7d streak" in t for t in texts)
