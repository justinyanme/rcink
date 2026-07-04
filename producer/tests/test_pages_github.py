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


def _find_hstack_with_texts(node: dict, values: set[str]) -> dict:
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == "hstack":
            texts = {c.get("value") for c in n.get("children", []) if c.get("type") == "text"}
            nested_texts = set()
            for child in n.get("children", []) or []:
                nested_texts.update(_all_text(child))
            if values <= (texts | nested_texts):
                return n
        stack.extend(n.get("children", []) or [])
    raise AssertionError(f"hstack with {values!r} not found")


def _make(
    *,
    today: int = 3,
    week: int = 18,
    streak: int = 5,
    total_14w: int = 142,
) -> GitHubActivity:
    return GitHubActivity(
        user="rocry",
        today_contributions=today,
        week_contributions=week,
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


def test_github_kpi_strip_distributes_left_center_right():
    page = build_github_page(_make(today=3, week=18, total_14w=142), now=dt.datetime(2026, 5, 23))
    row = _find_hstack_with_texts(page, {"TODAY", "WEEK", "14W"})

    assert [c["type"] for c in row["children"]] == ["vstack", "spacer", "vstack", "spacer", "vstack"]
    assert row["children"][0].get("align") == "start"
    assert row["children"][2].get("align") == "center"
    assert row["children"][4].get("align") == "end"


def test_github_page_user_line_shows_streak():
    page = build_github_page(_make(streak=7), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    assert any("@rocry" in t for t in texts)
    assert any("7d streak" in t for t in texts)


def test_github_page_uses_daily_heatmap_grid():
    page = build_github_page(_make(), now=dt.datetime(2026, 5, 23))
    texts = _all_text(page)
    grids = _find_nodes(page, "grid")

    assert any("CONTRIBS / DAY" in t for t in texts)
    assert len(grids) == 1
    assert grids[0]["cols"] == 14
    assert grids[0]["rows"] == 7
    assert grids[0]["values"] == [0] * (GRID_CELLS - 4) + [1, 5, 2, 8]
    assert _find_nodes(page, "sparkline") == []
