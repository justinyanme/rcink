import datetime as dt

from rcink.pages.overview import build_overview_page
from rcink.sources.calendar import CalEvent, TodaysCalendar
from rcink.sources.github import GRID_CELLS, GitHubActivity, RepoWeek
from rcink.sources.quotes import Quote
from rcink.sources.reminders import Reminder, TodaysReminders


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


def _reminder(title: str, *, updated_min_ago: int) -> Reminder:
    return Reminder(
        title=title,
        due=None,
        updated=_NOW - dt.timedelta(minutes=updated_min_ago),
        priority=0,
    )


def test_overview_todos_row_peeks_at_top_reminder():
    rem = TodaysReminders(
        list_name="Inbox",
        items=[_reminder("fix the foo bug", updated_min_ago=5)],
        incomplete_total=12,
    )
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=rem,
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )
    texts = _all_text(page)
    assert any("fix the foo bug" in t for t in texts)
    assert any(t == "12" for t in texts)
    # Relative-updated chip.
    assert any(t == "5m" for t in texts)


def test_overview_todos_row_all_clear_when_empty():
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=TodaysReminders(list_name="Inbox", items=[], incomplete_total=0),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )
    texts = _all_text(page)
    assert any("ALL CLEAR" in t for t in texts)


def test_overview_github_row_shows_week_and_top_repo():
    gh = GitHubActivity(
        user="rocry",
        today_commits=3,
        week_commits=18,
        streak=5,
        total_14w=142,
        total_year=420,
        contrib=[0] * GRID_CELLS,
        repos_week=[RepoWeek(name="rcpapercolor", commits=12)],
        langs_week=[],
    )
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        github=gh,
        quote=None,
        now=_NOW,
    )
    texts = _all_text(page)
    assert "3" in texts  # today
    assert "18" in texts  # week
    assert "rcpapercolor" in texts
    assert any("5d" in t for t in texts)
    # PR counts must NOT be on the row anymore.
    assert not any("/" in t and "PR" in t for t in texts)


def test_overview_github_row_no_top_repo_when_quiet_week():
    gh = GitHubActivity(
        user="rocry",
        today_commits=0,
        week_commits=0,
        streak=0,
        total_14w=0,
        total_year=0,
        contrib=[0] * GRID_CELLS,
        repos_week=[],
        langs_week=[],
    )
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        github=gh,
        quote=None,
        now=_NOW,
    )
    texts = _all_text(page)
    # Row still rendered (label + zero counts), but no "top:" prefix.
    assert any(t == "GITHUB" for t in texts)
    assert not any(t.startswith("top:") for t in texts)


def test_overview_todos_row_truncates_long_title():
    long_title = "rewrite the producer pipeline to support multi-list reminders"
    rem = TodaysReminders(
        list_name="Inbox",
        items=[_reminder(long_title, updated_min_ago=2)],
        incomplete_total=1,
    )
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=rem,
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )
    texts = _all_text(page)
    # Some node ends with ellipsis (overflow truncation).
    assert any(t.endswith("…") for t in texts)


def _ev(when_str: str, title: str, *, all_day: bool = False) -> CalEvent:
    start = dt.datetime.fromisoformat(when_str)
    return CalEvent(
        start=start,
        end=start + dt.timedelta(minutes=30),
        title=title,
        calendar="Personal",
        is_all_day=all_day,
    )


def test_overview_next_row_soon_when_today_empty_but_upcoming():
    cal = TodaysCalendar(
        today=_NOW.date(),
        events=[],
        upcoming=[_ev("2026-05-25T09:00", "Standup")],
    )
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=cal,
        reminders=_empty_reminders(),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )
    texts = _all_text(page)
    assert "SOON" in texts
    assert "Standup" in texts
    assert any("MON" in t and "09:00" in t for t in texts)


def test_overview_next_row_nothing_this_week_when_all_empty():
    cal = TodaysCalendar(today=_NOW.date(), events=[], upcoming=[])
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=cal,
        reminders=_empty_reminders(),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )
    texts = _all_text(page)
    assert any("nothing this week" in t for t in texts)


def test_overview_next_row_today_event_unchanged():
    cal = TodaysCalendar(
        today=_NOW.date(),
        events=[_ev("2026-05-23T11:00", "Lunch")],
        upcoming=[],
    )
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=cal,
        reminders=_empty_reminders(),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )
    texts = _all_text(page)
    assert "NEXT" in texts
    assert "Lunch" in texts
