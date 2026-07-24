import datetime as dt

from rcink.pages.overview import build_overview_page
from rcink.sources.calendar import CalEvent, TodaysCalendar
from rcink.sources.claude import ClaudeUsage
from rcink.sources.codex import CodexUsage
from rcink.sources.github import GRID_CELLS, GitHubActivity, RepoWeek
from rcink.sources.quotes import Quote
from rcink.sources.reminders import Reminder, TodaysReminders
from rcink.sources.weather import Weather


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


def _find_nodes(node: dict, typ: str) -> list[dict]:
    out: list[dict] = []
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == typ:
            out.append(n)
        stack.extend(n.get("children", []) or [])
    return out


def _find_hstack_with_direct_text(node: dict, value: str) -> dict:
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == "hstack" and any(c.get("value") == value for c in n.get("children", [])):
            return n
        stack.extend(n.get("children", []) or [])
    raise AssertionError(f"hstack containing direct text {value!r} not found")


def _overview_quote_nodes(page: dict) -> list[dict]:
    children = page["children"]
    if len(children) < 3 or children[1].get("type") != "spacer" or children[1].get("size") != 2:
        return []
    out: list[dict] = []
    for child in children[2:]:
        if child.get("type") == "divider":
            break
        if child.get("type") == "text":
            out.append(child)
    return out


def _rendered_quote_text(nodes: list[dict]) -> str:
    return " ".join(n["value"].strip('"') for n in nodes)


_NOW = dt.datetime(2026, 5, 23, 9, 0)


def _empty_cal() -> TodaysCalendar:
    return TodaysCalendar(today=_NOW.date(), events=[], upcoming=[])


def _empty_reminders() -> TodaysReminders:
    return TodaysReminders(list_name="Inbox", items=[], incomplete_total=0)


def _empty_gh() -> GitHubActivity:
    return GitHubActivity(
        user="rocry",
        today_contributions=0,
        week_contributions=0,
        streak=0,
        total_14w=0,
        total_year=0,
        daily_counts=[0] * GRID_CELLS,
    )


def _weather() -> Weather:
    return Weather(
        location="LONDON",
        temp=28.4,
        high=31.2,
        low=24.6,
        unit="C",
        code=80,
        precip_probability=67,
        is_day=True,
        observed_at=_NOW.replace(tzinfo=dt.UTC),
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


def test_overview_long_quote_resizes_and_keeps_all_text():
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
    quote_nodes = _overview_quote_nodes(page)
    assert quote_nodes
    assert {n.get("size") for n in quote_nodes} == {"body"}
    assert _rendered_quote_text(quote_nodes) == long_q
    assert not any("…" in n["value"] for n in quote_nodes)


def test_overview_quote_too_long_fails_instead_of_truncating():
    too_long = " ".join(["visibility"] * 60)
    try:
        build_overview_page(
            claude=None,
            codex=None,
            calendar=_empty_cal(),
            reminders=_empty_reminders(),
            github=_empty_gh(),
            quote=Quote(lines=[too_long], attrib="anon"),
            now=_NOW,
        )
    except ValueError as exc:
        assert "overview quote is too long to fit" in str(exc)
    else:
        raise AssertionError("long overview quote should fail rather than truncate")


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
        today_contributions=3,
        week_contributions=18,
        streak=5,
        total_14w=142,
        total_year=420,
        daily_counts=[0] * GRID_CELLS,
        repos_week=[RepoWeek(name="rcpapercolor", commits=12)],
        langs_week=[],
    )
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        github=gh,
        weather=_weather(),
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


def test_overview_weather_row_shows_temp_range_and_pop():
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        weather=_weather(),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )

    texts = _all_text(page)
    assert "LONDON" in texts
    assert "28C" in texts
    assert "RAIN" in texts
    assert "25~31C" in texts
    assert "RAIN 67%" in texts
    assert "WEATHER" not in texts
    assert "POP 67%" not in texts


def test_overview_weather_row_hides_low_precipitation():
    weather = _weather()
    weather.code = 0
    weather.precip_probability = 2
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        weather=weather,
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )

    row = _find_hstack_with_direct_text(page, "LONDON")
    texts = [n["value"] for n in row["children"] if n.get("type") == "text"]
    assert "LONDON" in texts
    assert "SUN" in texts
    assert "RAIN 2%" not in texts
    assert not any(t.startswith("POP") for t in texts)


def test_overview_weather_config_issue_is_visible():
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        weather=None,
        weather_issue="config",
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )

    row = _find_hstack_with_direct_text(page, "WEATHER")
    texts = [n["value"] for n in row["children"] if n.get("type") == "text"]
    assert "WEATHER" in texts
    assert "CONFIG" in texts
    assert "SET LAT LON" in texts
    assert "—" not in texts


def test_overview_weather_fetch_issue_is_visible():
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        weather=None,
        weather_issue="fetch",
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )

    row = _find_hstack_with_direct_text(page, "WEATHER")
    texts = [n["value"] for n in row["children"] if n.get("type") == "text"]
    assert "WEATHER" in texts
    assert "ERROR" in texts
    assert "FETCH FAILED" in texts
    assert "—" not in texts


def test_overview_rows_have_no_letter_icon_markers_and_labels_are_sm():
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=_empty_cal(),
        reminders=TodaysReminders(
            list_name="Inbox", items=[_reminder("fix bug", updated_min_ago=5)], incomplete_total=12
        ),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )

    assert _find_nodes(page, "icon") == []

    label_values = {
        "CLAUDE",
        "CODEX",
        "NEXT",
        "TODO",
        "GITHUB",
        "WEATHER",
    }
    label_nodes = [n for n in _all_text_nodes(page) if n.get("value") in label_values]
    assert label_nodes
    assert {n.get("value") for n in label_nodes} == label_values
    assert all(n.get("size") == "sm" for n in label_nodes)

    body_row_values = {
        "5H",
        "7D",
        "12",
    }
    row_text_nodes = [n for n in _all_text_nodes(page) if n.get("value") in body_row_values]
    assert row_text_nodes
    assert all(n.get("size") == "body" for n in row_text_nodes)
    assert not any(n.get("size") == "h2" for n in _all_text_nodes(page))


def test_overview_usage_reset_times_are_compact():
    page = build_overview_page(
        claude=ClaudeUsage(
            five_hour_pct=42,
            seven_day_pct=68,
            scoped_limits=[],
            five_hour_reset="1h 39m",
            seven_day_reset="5d 4h",
        ),
        codex=CodexUsage(
            primary_pct=70,
            primary_reset="1h 0m",
            primary_window_min=300,
            secondary_pct=75,
            secondary_reset="5d 21h",
            secondary_window_min=10080,
            credits_dollars=None,
        ),
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )

    texts = _all_text(page)
    assert "1h39m" in texts
    assert "1h0m" in texts
    assert "1h 39m" not in texts
    assert "1h 0m" not in texts


def test_overview_uses_actual_single_codex_window_label():
    page = build_overview_page(
        claude=None,
        codex=CodexUsage(
            primary_pct=12,
            primary_reset="4d 18h",
            primary_window_min=10080,
            secondary_pct=None,
            secondary_reset="",
            secondary_window_min=None,
            credits_dollars=None,
        ),
        calendar=_empty_cal(),
        reminders=_empty_reminders(),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )

    row = _find_hstack_with_direct_text(page, "CODEX")
    texts = [node["value"] for node in row["children"] if node.get("type") == "text"]
    assert "7D" in texts
    assert "5H" not in texts
    assert texts.count("7D") == 1


def test_overview_github_row_uses_smaller_font():
    gh = GitHubActivity(
        user="rocry",
        today_contributions=12,
        week_contributions=113,
        streak=151,
        total_14w=4468,
        total_year=9000,
        daily_counts=[0] * GRID_CELLS,
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

    row = _find_hstack_with_direct_text(page, "GITHUB")
    values = {"GITHUB", "12", "today", "113", "week", "151d"}
    nodes = [n for n in row["children"] if n.get("value") in values]
    assert {n.get("value") for n in nodes} == values
    assert all(n.get("size") == "sm" for n in nodes)


def test_overview_calendar_and_todo_titles_share_chip_row_padding():
    cal = TodaysCalendar(
        today=_NOW.date(),
        events=[_ev("2026-05-23T11:00", "Lunch")],
        upcoming=[],
    )
    page = build_overview_page(
        claude=None,
        codex=None,
        calendar=cal,
        reminders=TodaysReminders(
            list_name="Inbox", items=[_reminder("fix bug", updated_min_ago=5)], incomplete_total=12
        ),
        github=_empty_gh(),
        quote=None,
        now=_NOW,
    )

    next_row = _find_hstack_with_direct_text(page, "NEXT")
    todo_row = _find_hstack_with_direct_text(page, "TODO")

    next_chip = next(c for c in next_row["children"] if c.get("value") == "NEXT")
    next_title = next(c for c in next_row["children"] if c.get("value") == "Lunch")
    todo_chip = next(c for c in todo_row["children"] if c.get("value") == "TODO")
    todo_title = next(c for c in todo_row["children"] if "fix bug" in c.get("value", ""))

    assert next_chip.get("pad") == 4
    assert "dy" not in next_chip
    assert next_title.get("pad") == 4
    assert next_title.get("dy") == 2
    assert todo_chip.get("pad") == 4
    assert "dy" not in todo_chip
    assert todo_title.get("pad") == 4
    assert todo_title.get("dy") == 2


def test_overview_github_row_no_top_repo_when_quiet_week():
    gh = GitHubActivity(
        user="rocry",
        today_contributions=0,
        week_contributions=0,
        streak=0,
        total_14w=0,
        total_year=0,
        daily_counts=[0] * GRID_CELLS,
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
