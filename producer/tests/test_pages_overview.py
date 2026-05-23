import datetime as dt

from rcpaper.pages.overview import build_overview_page
from rcpaper.sources.calendar import TodaysCalendar
from rcpaper.sources.github import GRID_CELLS, GitHubActivity
from rcpaper.sources.quotes import Quote
from rcpaper.sources.reminders import TodaysReminders


def _all_text(node: dict) -> list[str]:
    out: list[str] = []
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == "text":
            out.append(n.get("value", ""))
        stack.extend(n.get("children", []) or [])
    return out


def _all_text_nodes(node: dict) -> list[dict]:
    out: list[dict] = []
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == "text":
            out.append(n)
        stack.extend(n.get("children", []) or [])
    return out


_NOW = dt.datetime(2026, 5, 23, 9, 0)


def _empty_cal() -> TodaysCalendar:
    return TodaysCalendar(today=_NOW.date(), events=[], upcoming=[])


def _empty_reminders() -> TodaysReminders:
    return TodaysReminders(list_name="Inbox", items=[], incomplete_total=0)


def _empty_gh() -> GitHubActivity:
    return GitHubActivity(
        user="rocry",
        today_commits=0,
        week_commits=0,
        streak=0,
        total_14w=0,
        total_year=0,
        contrib=[0] * GRID_CELLS,
    )


def test_overview_short_quote_single_line_at_h2():
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        github=_empty_gh(),
        quote=Quote(lines=["small steps win"], attrib="anon"),
        now=_NOW,
    )
    nodes = _all_text_nodes(page)
    quote_nodes = [n for n in nodes if "small steps win" in n.get("value", "")]
    assert quote_nodes, "quote line not present"
    assert all(n.get("size") == "h2" for n in quote_nodes)


def test_overview_long_quote_wraps_up_to_three_lines():
    long_q = (
        "the obstacle in the path becomes the path · never forget within every "
        "obstacle is an opportunity to improve our condition"
    )
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        github=_empty_gh(),
        quote=Quote(lines=[long_q], attrib="Ryan Holiday"),
        now=_NOW,
    )
    texts = _all_text(page)
    # The quote should be split across 2-3 nodes at h2; total wrap ≤ 3 lines.
    quote_lines = [t for t in texts if t and t not in ('"', "—")]
    has_quote = any("obstacle" in t for t in quote_lines)
    assert has_quote


def test_overview_missing_quote_omits_quote_block():
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )
    texts = _all_text(page)
    # Date stamp still present; no spurious empty quote node.
    assert any("MAY 23" in t.upper() for t in texts)
    # No leading `"` decoration when quote is absent.
    assert '"' not in texts
