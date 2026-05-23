# Overview + Pages Tweaks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Producer-side tweaks bundled together: calendar rolls over to upcoming days when today is empty; GitHub view rebuilt around velocity (week commits + top repos + languages) and drops PR action-items; quote on overview goes from one truncated line to up to three wrapped `h2` lines; reminder row on overview peeks at the top item title.

**Architecture:** Pure producer Python changes. Sources (`rcpaper.sources.*`) gain new fields, pages (`rcpaper.pages.*`) consume them; firmware-side primitives (`hbar`, `grid`, `h1/h2/body/sm`) already support everything. Shared text-wrap helper extracted from `pages/todo.py` into `pages/_common.py` so todo + overview-quote wrap identically.

**Tech Stack:** Python 3.13, `uv`, `pytest`, `httpx` (GitHub GraphQL), PyObjC (EventKit). Tests run via `uv run pytest` from `producer/`.

Spec: `docs/superpowers/specs/2026-05-23-overview-and-pages-tweaks-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `producer/rcpaper/pages/_common.py` | modify | Add `cell_width`, `visual_width`, `wrap_visual`, `relative_short` |
| `producer/rcpaper/pages/todo.py` | modify | Replace local copies of those helpers with imports |
| `producer/rcpaper/sources/calendar.py` | modify | 7-day window; split `events` / `upcoming` |
| `producer/rcpaper/pages/calendar.py` | modify | UPCOMING branch when today empty |
| `producer/rcpaper/sources/github.py` | modify | New GraphQL alias; `RepoWeek`, `LangWeek`; drop PR fields; aggregation |
| `producer/rcpaper/pages/github.py` | rewrite | New layout: KPI strip / REPOS / LANGUAGES / heatmap |
| `producer/scripts/github_fetch.py` | modify | Log line drops PRs, adds week + repo count |
| `producer/rcpaper/pages/overview.py` | modify | Header split, quote block, todos peek, github velocity, SOON branch |
| `producer/tests/test_pages_common.py` | create | `wrap_visual` + `relative_short` tests |
| `producer/tests/test_pages_calendar.py` | create | Empty-today branches |
| `producer/tests/test_sources_github.py` | extend | Language-weight aggregation, short-repo-name, empty week |
| `producer/tests/test_pages_github.py` | create | KPI strip, REPOS/LANGUAGES sections, empty-week omission |
| `producer/tests/test_pages_overview.py` | create | Quote wrap, todos peek, github row, SOON branch |

Tasks 1, 2, 4 are foundation; pages depend on their sources. Order below matches the dependency chain.

---

## Task 1: Extract shared text helpers into `pages/_common.py`

Move `cell_width`, `visual_width`, `wrap-to-N-lines`, and `relative_short` out of `pages/todo.py` so `pages/overview.py` can reuse them.

**Files:**
- Modify: `producer/rcpaper/pages/_common.py`
- Modify: `producer/rcpaper/pages/todo.py`
- Create: `producer/tests/test_pages_common.py`

- [ ] **Step 1: Write failing tests for `wrap_visual` and `relative_short`**

Create `producer/tests/test_pages_common.py`:

```python
import datetime as dt

from rcpaper.pages._common import relative_short, visual_width, wrap_visual


def test_wrap_visual_short_string_single_line():
    assert wrap_visual("hello world", line_limit=20, max_lines=3) == ["hello world"]


def test_wrap_visual_breaks_on_space():
    out = wrap_visual("hello world foo bar baz", line_limit=11, max_lines=3)
    assert out == ["hello world", "foo bar baz"]


def test_wrap_visual_caps_at_max_lines_with_ellipsis():
    out = wrap_visual(
        "alpha bravo charlie delta echo foxtrot golf hotel",
        line_limit=12,
        max_lines=2,
    )
    assert len(out) == 2
    assert out[-1].endswith("…")


def test_wrap_visual_cjk_counts_two_cells():
    # 6 wide chars = 12 cells; with line_limit=8 we should wrap after 4 chars.
    out = wrap_visual("一二三四五六", line_limit=8, max_lines=2)
    assert len(out) == 2
    assert visual_width(out[0]) <= 8
    assert visual_width(out[1]) <= 8


def test_wrap_visual_hard_break_when_no_spaces():
    # No whitespace at all — fall back to character break.
    out = wrap_visual("abcdefghijkl", line_limit=4, max_lines=3)
    assert out == ["abcd", "efgh", "ijkl"]


def test_wrap_visual_max_lines_one_truncates():
    out = wrap_visual("alpha bravo charlie", line_limit=10, max_lines=1)
    assert len(out) == 1
    assert out[0].endswith("…")


def test_relative_short_minutes():
    now = dt.datetime(2026, 5, 23, 12, 0, tzinfo=dt.timezone.utc)
    assert relative_short(now - dt.timedelta(minutes=5), now=now) == "5m"


def test_relative_short_hours():
    now = dt.datetime(2026, 5, 23, 12, 0, tzinfo=dt.timezone.utc)
    assert relative_short(now - dt.timedelta(hours=3), now=now) == "3h"


def test_relative_short_none():
    now = dt.datetime(2026, 5, 23, 12, 0, tzinfo=dt.timezone.utc)
    assert relative_short(None, now=now) == ""


def test_relative_short_now():
    now = dt.datetime(2026, 5, 23, 12, 0, tzinfo=dt.timezone.utc)
    assert relative_short(now, now=now) == "now"
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd producer && uv run pytest tests/test_pages_common.py -v
```

Expected: collection error / ImportError on `wrap_visual` and `relative_short` (don't exist in `_common.py` yet).

- [ ] **Step 3: Add the helpers to `pages/_common.py`**

Append to `producer/rcpaper/pages/_common.py` (keep the existing `kpi_row` function):

```python
import datetime as dt
import unicodedata


def cell_width(c: str) -> int:
    """1 for narrow / 2 for wide. Matches the VT323-vs-efontCN width ratio."""
    return 2 if unicodedata.east_asian_width(c) in ("W", "F") else 1


def visual_width(s: str) -> int:
    return sum(cell_width(c) for c in s)


def _trim(s: str, *, limit: int) -> str:
    """Truncate `s` to ≤ `limit` visual cells, appending `…` if cut."""
    if visual_width(s) <= limit:
        return s
    out = ""
    width = 0
    for c in s:
        w = cell_width(c)
        if width + w + 1 > limit:
            break
        out += c
        width += w
    return out.rstrip() + "…"


def wrap_visual(body: str, *, line_limit: int, max_lines: int) -> list[str]:
    """Greedy word-wrap by visual cells; CJK-aware.

    Breaks on whitespace when possible, falls back to hard character break for
    spaceless runs (CJK). Trailing overflow on the final line is truncated with
    `…`. Returns ≤ `max_lines` non-empty lines.
    """
    if max_lines <= 0:
        return []
    if visual_width(body) <= line_limit:
        return [body]

    lines: list[str] = []
    remaining = body
    while remaining and len(lines) < max_lines:
        if visual_width(remaining) <= line_limit:
            lines.append(remaining)
            break
        # If this is the last line we're allowed to emit, trim with ellipsis.
        if len(lines) == max_lines - 1:
            lines.append(_trim(remaining, limit=line_limit))
            return lines
        # Walk chars accumulating width; remember the last whitespace inside
        # the budget. Prefer breaking there.
        width = 0
        last_space = -1
        hard_break = len(remaining)
        for i, c in enumerate(remaining):
            w = cell_width(c)
            if width + w > line_limit:
                hard_break = i
                break
            width += w
            if c.isspace():
                last_space = i
        cut = last_space if last_space > 0 else hard_break
        lines.append(remaining[:cut].rstrip())
        remaining = remaining[cut:].lstrip()
    return lines


def relative_short(when: dt.datetime | None, *, now: dt.datetime) -> str:
    """Compact age label: '5m', '3h', '2d', '1w', '4mo'. Empty string for None."""
    if when is None:
        return ""
    delta = now - when
    seconds = max(0, int(delta.total_seconds()))
    if seconds < 60:
        return "now"
    minutes = seconds // 60
    if minutes < 60:
        return f"{minutes}m"
    hours = minutes // 60
    if hours < 24:
        return f"{hours}h"
    days = hours // 24
    if days < 7:
        return f"{days}d"
    weeks = days // 7
    if weeks < 5:
        return f"{weeks}w"
    months = max(1, days // 30)
    return f"{months}mo"
```

- [ ] **Step 4: Run tests, verify they pass**

```bash
cd producer && uv run pytest tests/test_pages_common.py -v
```

Expected: all 10 tests PASS.

- [ ] **Step 5: Migrate `pages/todo.py` to use the shared helpers**

In `producer/rcpaper/pages/todo.py`:

1. Replace the imports block top-of-file:

```python
from __future__ import annotations

import datetime as dt
import re
from urllib.parse import urlparse

from rcpaper.layout import divider, hbar, hstack, icon, spacer, text, vstack
from rcpaper.pages._common import cell_width, relative_short, visual_width, wrap_visual
from rcpaper.palette import ColorToken
from rcpaper.sources.reminders import Reminder, TodaysReminders
```

(Remove `import unicodedata`.)

2. Delete the local definitions of `_cell_width`, `_visual_width`, `_trim`, `_relative_short`. Keep `LINE_LIMIT = 28`, `VISIBLE_ITEMS = 10`, `_URL_RE`.

3. Replace `_split_two_lines` body with a thin call to `wrap_visual`:

```python
def _split_two_lines(body: str, *, line_limit: int) -> list[str]:
    return wrap_visual(body, line_limit=line_limit, max_lines=2)
```

4. Inside `_trim` callers (only one, in `render_title_node`), replace `_trim(residual, limit=line_limit)` with an inline-import from `_common`:

```python
from rcpaper.pages._common import _trim  # add to imports
```

Actually update the import line to: `from rcpaper.pages._common import _trim, cell_width, relative_short, visual_width, wrap_visual`.

5. Update other call sites: `_cell_width(c)` → `cell_width(c)`, `_visual_width(s)` → `visual_width(s)`, `_relative_short(...)` → `relative_short(...)`.

- [ ] **Step 6: Run full test suite to confirm no regression**

```bash
cd producer && uv run pytest -v
```

Expected: all existing `test_pages_todo.py` tests PASS, `test_pages_common.py` PASS. No new failures.

- [ ] **Step 7: Commit**

```bash
git add producer/rcpaper/pages/_common.py producer/rcpaper/pages/todo.py producer/tests/test_pages_common.py
git commit -m "refactor(pages): extract wrap_visual + relative_short into _common"
```

---

## Task 2: Widen calendar source to 7-day window

Add `upcoming: list[CalEvent]` to `TodaysCalendar`. Keeps `events` as today-only for back-compat.

**Files:**
- Modify: `producer/rcpaper/sources/calendar.py`

Note: this source is macOS-only and depends on EventKit / live system calendars, so the change isn't unit-tested directly here — coverage comes via the calendar-page tests in Task 3, which can construct `TodaysCalendar` instances synthetically.

- [ ] **Step 1: Update the dataclass and fetch function**

In `producer/rcpaper/sources/calendar.py`:

1. Update `TodaysCalendar`:

```python
@dataclass(slots=True)
class TodaysCalendar:
    today: dt.date
    events: list[CalEvent]      # today only
    upcoming: list[CalEvent]    # days +1..+6, sorted; both timed and all-day
```

2. Replace `fetch_todays_events` so the predicate spans 7 days and the results are split:

```python
def fetch_todays_events(now: dt.datetime | None = None) -> TodaysCalendar:
    now = now or dt.datetime.now().astimezone()
    store = ek.EKEventStore.alloc().init()

    cal_obj = F.NSCalendar.currentCalendar()
    units = F.NSCalendarUnitYear | F.NSCalendarUnitMonth | F.NSCalendarUnitDay
    today_comps = cal_obj.components_fromDate_(units, F.NSDate.date())
    midnight = cal_obj.dateFromComponents_(today_comps)
    week_end = midnight.dateByAddingTimeInterval_(7 * 86400)

    cals = store.calendarsForEntityType_(ek.EKEntityTypeEvent)
    pred = store.predicateForEventsWithStartDate_endDate_calendars_(midnight, week_end, cals)
    ns_events = store.eventsMatchingPredicate_(pred) or []

    today = now.date()
    all_events: list[CalEvent] = []
    for ev in ns_events:
        title = (ev.title() or "").strip()
        if not title:
            continue
        cal_name = ev.calendar().title() if ev.calendar() else ""
        if cal_name.startswith("Weather for"):
            continue
        all_events.append(
            CalEvent(
                start=_nsdate_to_dt(ev.startDate()),
                end=_nsdate_to_dt(ev.endDate()),
                title=title,
                calendar=cal_name,
                is_all_day=bool(ev.isAllDay()),
            )
        )
    all_events.sort(key=lambda e: e.start)

    events_today = [e for e in all_events if e.start.date() == today]
    events_upcoming = [e for e in all_events if e.start.date() > today]
    return TodaysCalendar(today=today, events=events_today, upcoming=events_upcoming)
```

- [ ] **Step 2: Verify nothing imports broke**

```bash
cd producer && uv run python -c "from rcpaper.sources.calendar import TodaysCalendar; print(TodaysCalendar.__annotations__)"
```

Expected: prints a dict containing `events` and `upcoming`. (Skip if not on macOS — the module guards itself.)

- [ ] **Step 3: Run existing tests**

```bash
cd producer && uv run pytest -v
```

Expected: all existing tests PASS. No callers of `TodaysCalendar` other than `pages/calendar.py` and `pages/overview.py`; both still see `events` populated identically to before.

- [ ] **Step 4: Commit**

```bash
git add producer/rcpaper/sources/calendar.py
git commit -m "feat(calendar): widen source window to 7 days, add upcoming list"
```

---

## Task 3: Calendar page — empty-today rollover

When today's events list is empty but `upcoming` has events, replace the empty-state hero with a compact `NO EVENTS · clear runway` line and a `UPCOMING` section listing the next 6 events with day-of-week prefixes. When the full week is empty, show `NOTHING UNTIL NEXT WEEK`.

**Files:**
- Modify: `producer/rcpaper/pages/calendar.py`
- Create: `producer/tests/test_pages_calendar.py`

- [ ] **Step 1: Write failing tests**

Create `producer/tests/test_pages_calendar.py`:

```python
import datetime as dt

from rcpaper.pages.calendar import build_calendar_page
from rcpaper.sources.calendar import CalEvent, TodaysCalendar


def _ev(when_str: str, title: str, *, duration_min: int = 30, all_day: bool = False) -> CalEvent:
    start = dt.datetime.fromisoformat(when_str)
    return CalEvent(
        start=start,
        end=start + dt.timedelta(minutes=duration_min),
        title=title,
        calendar="Personal",
        is_all_day=all_day,
    )


def _all_text(node: dict) -> list[str]:
    out: list[str] = []
    stack = [node]
    while stack:
        n = stack.pop()
        if n.get("type") == "text":
            out.append(n.get("value", ""))
        stack.extend(n.get("children", []) or [])
    return out


def test_calendar_today_has_events_unchanged():
    now = dt.datetime(2026, 5, 23, 9, 0)
    cal = TodaysCalendar(
        today=now.date(),
        events=[_ev("2026-05-23T10:00", "Standup")],
        upcoming=[],
    )
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    assert "NEXT" in texts
    assert "Standup" in texts


def test_calendar_today_empty_with_upcoming_renders_upcoming_section():
    now = dt.datetime(2026, 5, 23, 9, 0)  # Saturday
    cal = TodaysCalendar(
        today=now.date(),
        events=[],
        upcoming=[
            _ev("2026-05-25T09:00", "Standup"),       # Monday
            _ev("2026-05-26T14:00", "1:1 with Sam"),  # Tuesday
        ],
    )
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    assert "NO EVENTS" in texts
    assert "UPCOMING" in texts
    assert "Standup" in texts
    assert "1:1 with Sam" in texts
    # DOW prefix present.
    assert any(t.startswith("MON") for t in texts)
    assert any(t.startswith("TUE") for t in texts)


def test_calendar_empty_week_renders_nothing_until_next_week():
    now = dt.datetime(2026, 5, 23, 9, 0)
    cal = TodaysCalendar(today=now.date(), events=[], upcoming=[])
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    assert any("NOTHING UNTIL NEXT WEEK" in t for t in texts)


def test_calendar_upcoming_caps_at_six_rows():
    now = dt.datetime(2026, 5, 23, 9, 0)
    upcoming = [
        _ev(f"2026-05-25T{9 + i:02d}:00", f"event {i}")
        for i in range(10)
    ]
    cal = TodaysCalendar(today=now.date(), events=[], upcoming=upcoming)
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    # Only 6 of the titles should appear.
    visible_titles = [t for t in texts if t.startswith("event ")]
    assert len(visible_titles) == 6


def test_calendar_upcoming_all_day_event_uses_ALL():
    now = dt.datetime(2026, 5, 23, 9, 0)
    cal = TodaysCalendar(
        today=now.date(),
        events=[],
        upcoming=[_ev("2026-05-26T00:00", "Conference", all_day=True)],
    )
    page = build_calendar_page(cal, now=now)
    texts = _all_text(page)
    assert any(t.startswith("TUE") and "ALL" in t for t in texts)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd producer && uv run pytest tests/test_pages_calendar.py -v
```

Expected: most tests FAIL on missing `UPCOMING`, `NOTHING UNTIL NEXT WEEK`, etc. (Some pass if the today-has-events path is unchanged.)

- [ ] **Step 3: Update `pages/calendar.py`**

Replace the body of `build_calendar_page` in `producer/rcpaper/pages/calendar.py` (keep imports, helpers, and `_TICK_COLORS` at the top of the file):

```python
def build_calendar_page(cal: TodaysCalendar, *, now: dt.datetime) -> dict:
    today_label = cal.today.strftime("%a · %b %d").upper()

    children: list[dict] = [
        hstack(
            align="center",
            children=[
                text("TODAY", size="sm", weight="bold"),
                spacer(),
                text(today_label, size="sm", weight="bold"),
            ],
        ),
        spacer(size=2),
        divider(color="black", thickness=2),
        spacer(size=8),
    ]

    if cal.events:
        _append_today_body(children, cal.events, now)
    elif cal.upcoming:
        children += [
            hstack(
                align="center",
                gap=6,
                children=[
                    text("NO EVENTS", size="body", weight="bold", color="green"),
                    text("clear runway", size="sm"),
                ],
            ),
            spacer(size=10),
            divider(color="black", thickness=1),
            spacer(size=6),
            text("UPCOMING", size="sm", weight="bold"),
            spacer(size=2),
        ]
        for i, ev in enumerate(cal.upcoming[:6]):
            tick = _TICK_COLORS[i % len(_TICK_COLORS)]
            dow = ev.start.strftime("%a").upper()
            when = "ALL" if ev.is_all_day else ev.start.strftime("%H:%M")
            children.append(
                hstack(
                    gap=8,
                    align="center",
                    children=[
                        text(f"{dow} {when}", size="body", weight="bold", color=tick),
                        hbar(pct=100, color=tick, height=4),
                        text(ev.title, size="sm", weight="bold"),
                    ],
                )
            )
    else:
        children += [
            spacer(size=20),
            text("NOTHING UNTIL NEXT WEEK", size="h1", color="green", weight="bold", align="center"),
            spacer(size=8),
            text("clear runway", size="body", align="center"),
        ]

    return vstack(pad=18, gap=6, children=children)
```

And extract the existing today-has-events body (everything that was inside `else:` originally) into a helper `_append_today_body(children, events, now)`. Move the prior `if not cal.events: ... else: ...` content (the NEXT hero + UP NEXT loop) verbatim into:

```python
def _append_today_body(children: list[dict], events: list[CalEvent], now: dt.datetime) -> None:
    nxt = _next_event(events, now)
    if nxt is not None:
        children += [
            text("NEXT", size="sm", weight="bold", color="white", bg="red", pad=4),
            spacer(size=4),
            hstack(
                align="end",
                gap=6,
                children=[
                    text(_fmt_time(nxt.start, nxt.is_all_day), size="h1", color="red", weight="bold"),
                    vstack(
                        gap=2,
                        children=[
                            text(nxt.title, size="body", weight="bold"),
                            text(
                                f"{_fmt_duration(nxt.start, nxt.end, nxt.is_all_day)} · "
                                f"{_next_in_label(nxt.start, now)}",
                                size="sm",
                            ),
                        ],
                    ),
                ],
            ),
            spacer(size=10),
            divider(color="black", thickness=1),
            spacer(size=6),
        ]

    nxt_key = (nxt.start, nxt.title) if nxt is not None else None
    seen: set[tuple[dt.datetime, str]] = {nxt_key} if nxt_key else set()
    rest: list[CalEvent] = []
    for e in events:
        if e.end <= now and not e.is_all_day:
            continue
        key = (e.start, e.title)
        if key in seen:
            continue
        seen.add(key)
        rest.append(e)
    if rest:
        children.append(text("UP NEXT", size="sm", weight="bold"))
        children.append(spacer(size=2))
        for i, ev in enumerate(rest[:6]):
            tick = _TICK_COLORS[i % len(_TICK_COLORS)]
            children.append(
                hstack(
                    gap=8,
                    align="center",
                    children=[
                        text(_fmt_time(ev.start, ev.is_all_day), size="body", weight="bold", color=tick),
                        hbar(pct=100, color=tick, height=4),
                        text(ev.title, size="sm", weight="bold"),
                    ],
                )
            )
```

- [ ] **Step 4: Run tests to confirm they pass**

```bash
cd producer && uv run pytest tests/test_pages_calendar.py -v
```

Expected: all 5 tests PASS.

- [ ] **Step 5: Full suite check**

```bash
cd producer && uv run pytest -v
```

Expected: no regressions.

- [ ] **Step 6: Commit**

```bash
git add producer/rcpaper/pages/calendar.py producer/tests/test_pages_calendar.py
git commit -m "feat(calendar): show upcoming events when today is empty"
```

---

## Task 4: GitHub source — week query + repos/langs aggregation, drop PR fields

Add a second aliased `contributionsCollection(from, to)` to the GraphQL query, compute `week_commits`, `repos_week`, `langs_week`. Drop `pr_open` and `pr_review`.

**Files:**
- Modify: `producer/rcpaper/sources/github.py`
- Modify: `producer/tests/test_sources_github.py`

- [ ] **Step 1: Write failing tests for the new aggregation helpers**

Add to `producer/tests/test_sources_github.py`:

```python
from rcpaper.sources.github import (
    GRID_CELLS,
    LangWeek,
    RepoWeek,
    _aggregate_languages,
    _compute_streak,
    _normalize,
    _short_repo_name,
)


def test_short_repo_name_strips_own_owner():
    assert _short_repo_name("rocry/rcpapercolor", user="rocry") == "rcpapercolor"


def test_short_repo_name_keeps_foreign_owner():
    assert _short_repo_name("m5stack/M5GFX", user="rocry") == "m5stack/M5GFX"


def test_aggregate_languages_weighted_by_commits():
    # Repo A: 8 commits, Go=800 bytes, Python=200 bytes.
    # Repo B: 2 commits, Go=0,   Python=1000 bytes.
    # Commit-weighted size: Go = 8*800 = 6400, Python = 8*200 + 2*1000 = 3600.
    # Total = 10000; pcts = Go 64, Python 36.
    repos = [
        ("rocry/a", 8, [("Go", 800), ("Python", 200)]),
        ("rocry/b", 2, [("Python", 1000)]),
    ]
    langs = _aggregate_languages(repos)
    by_name = {l.name: l.pct for l in langs}
    assert by_name["Go"] == 64
    assert by_name["Python"] == 36


def test_aggregate_languages_empty():
    assert _aggregate_languages([]) == []


def test_aggregate_languages_caps_to_top_five():
    repos = [
        ("rocry/a", 1, [(f"L{i}", 1000 - i) for i in range(8)]),
    ]
    langs = _aggregate_languages(repos)
    assert len(langs) == 5
    assert [l.name for l in langs] == ["L0", "L1", "L2", "L3", "L4"]
```

Verify the dataclass exports exist:

```python
def test_lang_week_and_repo_week_exported():
    assert RepoWeek(name="x", commits=1).commits == 1
    assert LangWeek(name="Go", pct=10).pct == 10
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd producer && uv run pytest tests/test_sources_github.py -v
```

Expected: ImportError or AttributeError on `RepoWeek`, `LangWeek`, `_aggregate_languages`, `_short_repo_name`.

- [ ] **Step 3: Rewrite `producer/rcpaper/sources/github.py`**

Replace the file's contents:

```python
"""Fetch GitHub activity (commits today, week breakdown, streak, heatmap).

Two aliased queries in one GraphQL roundtrip:
  - the rolling 12-month contributionCalendar (drives the 14w heatmap + streak)
  - the rolling 7-day commitContributionsByRepository (drives weekly velocity)

GITHUB_TOKEN needs `read:user`; GITHUB_USERNAME is the login to report on.
"""

from __future__ import annotations

import datetime as dt
import os
from dataclasses import dataclass, field

import httpx

GRAPHQL_URL = "https://api.github.com/graphql"

_QUERY = """
query($user: String!, $weekFrom: DateTime!, $weekTo: DateTime!) {
  user(login: $user) {
    contributionsCollection {
      contributionCalendar {
        totalContributions
        weeks {
          contributionDays { date contributionCount }
        }
      }
    }
    week: contributionsCollection(from: $weekFrom, to: $weekTo) {
      commitContributionsByRepository(maxRepositories: 10) {
        repository {
          nameWithOwner
          languages(first: 5, orderBy: {field: SIZE, direction: DESC}) {
            edges { size node { name } }
          }
        }
        contributions { totalCount }
      }
    }
  }
}
"""

# 14 cols × 7 rows = 98 cells — matches the device-side grid primitive footprint.
GRID_WEEKS = 14
GRID_CELLS = GRID_WEEKS * 7

# How many top repos / languages we surface on the page.
TOP_REPOS = 5
TOP_LANGS = 5


@dataclass(slots=True)
class RepoWeek:
    name: str       # short display name (see _short_repo_name)
    commits: int


@dataclass(slots=True)
class LangWeek:
    name: str       # e.g. "Go", "Python"
    pct: int        # 0..100, top-5 normalised to sum ≤ 100


@dataclass(slots=True)
class GitHubActivity:
    user: str
    today_commits: int
    week_commits: int
    streak: int
    total_14w: int
    total_year: int
    contrib: list[int]
    repos_week: list[RepoWeek] = field(default_factory=list)
    langs_week: list[LangWeek] = field(default_factory=list)


def _env(key: str) -> str:
    v = os.environ.get(key)
    if not v:
        raise RuntimeError(f"{key} not set in environment")
    return v


def _normalize(counts: list[int]) -> list[int]:
    if not counts:
        return []
    max_v = max(counts) or 1
    return [min(100, int(round((c / max_v) * 100))) for c in counts]


def _compute_streak(days: list[tuple[str, int]]) -> int:
    if not days:
        return 0
    i = len(days) - 1
    streak = 0
    today_zero_grace = days[i][1] == 0
    if today_zero_grace:
        i -= 1
    while i >= 0 and days[i][1] > 0:
        streak += 1
        i -= 1
    return streak


def _short_repo_name(name_with_owner: str, *, user: str) -> str:
    """`owner/repo` → `repo` when owner == user (case-insensitive), else unchanged."""
    if "/" not in name_with_owner:
        return name_with_owner
    owner, repo = name_with_owner.split("/", 1)
    if owner.lower() == user.lower():
        return repo
    return name_with_owner


def _aggregate_languages(
    repos: list[tuple[str, int, list[tuple[str, int]]]],
) -> list[LangWeek]:
    """Commit-weighted language aggregation.

    `repos` is a list of (display_name, commits_this_week, [(lang_name, size_bytes), ...]).
    For each repo we multiply each language size by the repo's commit count,
    sum across repos, take top 5 by weighted size, normalize to integer
    percentages (top-5 share, not total share).
    """
    weighted: dict[str, int] = {}
    for _name, commits, langs in repos:
        for lang, size in langs:
            weighted[lang] = weighted.get(lang, 0) + commits * size
    if not weighted:
        return []
    top = sorted(weighted.items(), key=lambda kv: kv[1], reverse=True)[:TOP_LANGS]
    total = sum(size for _, size in top) or 1
    return [LangWeek(name=name, pct=int(round(size * 100 / total))) for name, size in top]


def fetch_github_activity(
    *,
    user: str | None = None,
    token: str | None = None,
    now: dt.datetime | None = None,
) -> GitHubActivity:
    user = user or _env("GITHUB_USERNAME")
    token = token or _env("GITHUB_TOKEN")
    now = now or dt.datetime.now(tz=dt.timezone.utc)
    week_from = (now - dt.timedelta(days=7)).strftime("%Y-%m-%dT%H:%M:%SZ")
    week_to = now.strftime("%Y-%m-%dT%H:%M:%SZ")

    r = httpx.post(
        GRAPHQL_URL,
        json={
            "query": _QUERY,
            "variables": {"user": user, "weekFrom": week_from, "weekTo": week_to},
        },
        headers={
            "Authorization": f"Bearer {token}",
            "User-Agent": "rcpapercolor-producer",
        },
        timeout=30,
    )
    if r.status_code != 200:
        raise RuntimeError(f"GitHub GraphQL HTTP {r.status_code}: {r.text}")
    payload = r.json()
    if "errors" in payload:
        raise RuntimeError(f"GitHub GraphQL errors: {payload['errors']}")

    u = payload["data"]["user"]
    if u is None:
        raise RuntimeError(f"GitHub user not found: {user}")

    cal = u["contributionsCollection"]["contributionCalendar"]
    days: list[tuple[str, int]] = []
    for week in cal["weeks"]:
        for d in week["contributionDays"]:
            days.append((d["date"], int(d["contributionCount"])))

    today_commits = days[-1][1] if days else 0
    streak = _compute_streak(days)

    window_days = days[-GRID_CELLS:]
    window_counts = [c for _, c in window_days]
    total_14w = sum(window_counts)
    contrib = _normalize(window_counts)
    while len(contrib) < GRID_CELLS:
        contrib.insert(0, 0)

    week_buckets = u.get("week", {}).get("commitContributionsByRepository") or []
    raw_repos: list[tuple[str, int, list[tuple[str, int]]]] = []
    for bucket in week_buckets:
        commits = int(bucket["contributions"]["totalCount"])
        if commits <= 0:
            continue
        nwo = bucket["repository"]["nameWithOwner"]
        langs = [
            (edge["node"]["name"], int(edge["size"]))
            for edge in (bucket["repository"]["languages"]["edges"] or [])
        ]
        raw_repos.append((_short_repo_name(nwo, user=user), commits, langs))
    raw_repos.sort(key=lambda r: r[1], reverse=True)
    week_commits = sum(c for _, c, _ in raw_repos)
    repos_week = [RepoWeek(name=n, commits=c) for n, c, _ in raw_repos[:TOP_REPOS]]
    langs_week = _aggregate_languages(raw_repos[:TOP_REPOS])

    return GitHubActivity(
        user=user,
        today_commits=today_commits,
        week_commits=week_commits,
        streak=streak,
        total_14w=total_14w,
        total_year=int(cal["totalContributions"]),
        contrib=contrib,
        repos_week=repos_week,
        langs_week=langs_week,
    )
```

- [ ] **Step 4: Run tests, confirm pass**

```bash
cd producer && uv run pytest tests/test_sources_github.py -v
```

Expected: all tests PASS (existing streak/normalize + new aggregation + dataclass exports).

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/sources/github.py producer/tests/test_sources_github.py
git commit -m "feat(github source): add week breakdown, top repos, weighted langs; drop PR counts"
```

---

## Task 5: GitHub page — rewrite for velocity

KPI strip (TODAY / WEEK / 14W), REPOS·7D bar chart, LANGUAGES·7D legend, heatmap. Drops PR display.

**Files:**
- Rewrite: `producer/rcpaper/pages/github.py`
- Create: `producer/tests/test_pages_github.py`

- [ ] **Step 1: Write failing tests**

Create `producer/tests/test_pages_github.py`:

```python
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
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd producer && uv run pytest tests/test_pages_github.py -v
```

Expected: tests FAIL (current page references removed fields like `pr_open`).

- [ ] **Step 3: Rewrite `producer/rcpaper/pages/github.py`**

Replace contents with:

```python
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
    total_pct = sum(l.pct for l in langs) or 1
    legend_children: list[dict] = []
    for i, l in enumerate(langs):
        color = _LANG_COLORS[i % len(_LANG_COLORS)]
        if i > 0:
            legend_children.append(text("·", size="sm"))
        legend_children.append(text(f"{l.name} {l.pct}", size="sm", weight="bold", color=color))
    return [
        text("LANGUAGES · 7D", size="sm", weight="bold"),
        spacer(size=2),
        hbar(pct=min(100.0, total_pct), color="green", height=10),
        spacer(size=2),
        hstack(gap=6, align="center", children=legend_children),
    ]


def build_github_page(activity: GitHubActivity, *, now: dt.datetime) -> dict:
    streak_color: ColorToken = (
        "red" if activity.streak >= 7 else "yellow" if activity.streak >= 3 else "green"
    )
    streak_text = (
        f"{activity.streak}d streak" if activity.streak > 0 else "no streak"
    )

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
```

- [ ] **Step 4: Run tests, confirm pass**

```bash
cd producer && uv run pytest tests/test_pages_github.py -v
```

Expected: all 6 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/pages/github.py producer/tests/test_pages_github.py
git commit -m "feat(github page): rebuild as velocity dashboard (KPIs, repos, langs)"
```

---

## Task 6: Update `scripts/github_fetch.py` log line

Drop the now-missing PR fields from the loguru info line.

**Files:**
- Modify: `producer/scripts/github_fetch.py`

- [ ] **Step 1: Replace the log call**

In `producer/scripts/github_fetch.py`, replace lines 17-23 (the `run` function body) with:

```python
@app.command()
def run() -> None:
    activity = fetch_github_activity()
    logger.info(
        f"github @{activity.user}: today={activity.today_commits} "
        f"week={activity.week_commits} streak={activity.streak}d "
        f"repos={len(activity.repos_week)} 14w={activity.total_14w}"
    )
    page = build_github_page(activity, now=dt.datetime.now())
    ingest(id="github", layout=page, order=50)
```

- [ ] **Step 2: Smoke-check imports**

```bash
cd producer && uv run python -c "from scripts.github_fetch import app; print('ok')"
```

Expected: prints `ok`. (No live fetch — just import.)

- [ ] **Step 3: Run full suite**

```bash
cd producer && uv run pytest -v
```

Expected: no new failures.

- [ ] **Step 4: Commit**

```bash
git add producer/scripts/github_fetch.py
git commit -m "chore(github fetch): drop PR fields from log line, add week/repos"
```

---

## Task 7: Overview — multi-line quote block

Move the date out of the inline-quote row; put the quote in its own `h2`, up-to-3-line block underneath.

**Files:**
- Modify: `producer/rcpaper/pages/overview.py`
- Create: `producer/tests/test_pages_overview.py`

- [ ] **Step 1: Write failing tests**

Create `producer/tests/test_pages_overview.py`:

```python
import datetime as dt

from rcpaper.pages.overview import build_overview_page
from rcpaper.sources.calendar import CalEvent, TodaysCalendar
from rcpaper.sources.github import GRID_CELLS, GitHubActivity
from rcpaper.sources.quotes import Quote
from rcpaper.sources.reminders import Reminder, TodaysReminders


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
    quote_lines = [t for t in texts if t and t not in ("\"", "—")]
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
    assert "\"" not in texts
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd producer && uv run pytest tests/test_pages_overview.py -v
```

Expected: tests FAIL — current `build_overview_page` returns the quote as `body` and inline (not `h2`), and the missing-quote assertions may collide with old structure.

- [ ] **Step 3: Update `pages/overview.py`**

Edit `producer/rcpaper/pages/overview.py`:

1. Update imports:

```python
from rcpaper.pages._common import wrap_visual
```

2. Replace `_HEADER_QUOTE_MAX = 30` block + `_quote_inline()` helper with:

```python
# Visual-cell budget for one wrapped quote line at h2-bold on a 540px panel.
_QUOTE_LINE_LIMIT = 30
_QUOTE_MAX_LINES = 3


def _quote_block(q: Quote | None) -> list[dict]:
    """Return 0..N text nodes for the multi-line quote block.

    Empty list when there's nothing to show — header omits the block entirely.
    """
    if q is None:
        return []
    joined = " ".join(line.strip() for line in q.lines if line.strip())
    if not joined:
        return []
    lines = wrap_visual(joined, line_limit=_QUOTE_LINE_LIMIT, max_lines=_QUOTE_MAX_LINES)
    out: list[dict] = []
    for i, line in enumerate(lines):
        prefix = '"' if i == 0 else ""
        suffix = '"' if i == len(lines) - 1 else ""
        out.append(text(f"{prefix}{line}{suffix}", size="h2", weight="bold"))
    return out
```

3. Inside `build_overview_page`, replace the header construction (the `inline = _quote_inline(quote)` block and the resulting `if inline: header = ... else: header = ...`) with:

```python
    header = hstack(
        align="center",
        children=[spacer(), date_label],
    )
    quote_nodes = _quote_block(quote)
```

4. In the `return vstack(...)` children list, insert `*quote_nodes` and a `spacer(size=2)` after `header` (only when quote_nodes is non-empty):

```python
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
        spacer(size=4),
        divider(color="black", thickness=1),
        spacer(size=4),
        _next_event_row(calendar, now),
        _todos_row(reminders),
        spacer(),
    ]
    return vstack(pad=16, gap=10, children=page_children)
```

- [ ] **Step 4: Run tests, confirm pass**

```bash
cd producer && uv run pytest tests/test_pages_overview.py -v
```

Expected: 3 quote tests PASS.

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/pages/overview.py producer/tests/test_pages_overview.py
git commit -m "feat(overview): multi-line h2 quote block with wrap helper"
```

---

## Task 8: Overview — reminder peek (top item title)

Show the top reminder title inline in the TODO row; relative-updated label on the right; `ALL CLEAR` when the list is empty.

**Files:**
- Modify: `producer/rcpaper/pages/overview.py`
- Modify: `producer/tests/test_pages_overview.py`

- [ ] **Step 1: Append failing tests**

Add to `producer/tests/test_pages_overview.py`:

```python
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
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd producer && uv run pytest tests/test_pages_overview.py::test_overview_todos_row_peeks_at_top_reminder \
    tests/test_pages_overview.py::test_overview_todos_row_all_clear_when_empty \
    tests/test_pages_overview.py::test_overview_todos_row_truncates_long_title -v
```

Expected: tests FAIL — current `_todos_row` doesn't include reminder title text, no `ALL CLEAR` token.

- [ ] **Step 3: Rewrite `_todos_row` in `pages/overview.py`**

Add to imports near top of file:

```python
from rcpaper.pages._common import relative_short, wrap_visual
```

Change `_todos_row` signature to accept `now`, and replace its body with the peek layout:

```python
_TODO_PEEK_LIMIT = 24


def _todos_row(rem: TodaysReminders | None, *, now: dt.datetime) -> dict:
    if rem is None:
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("todo", size=16, color="black"),
                text("TODO", size="sm", weight="bold", color="white", bg="black", pad=4),
                text("—", size="body", weight="bold"),
                spacer(),
            ],
        )
    count = rem.incomplete_total
    count_color: ColorToken = (
        "red" if count >= 20 else "yellow" if count >= 5 else "green"
    )
    children: list[dict] = [
        icon("todo", size=16, color="black"),
        text("TODO", size="sm", weight="bold", color="white", bg="black", pad=4),
        text(str(count), size="h2", color=count_color, weight="bold"),
    ]
    if not rem.items:
        children += [
            text("ALL CLEAR", size="body", weight="bold", color="green"),
            spacer(),
            text(rem.list_name, size="sm"),
        ]
        return hstack(gap=8, align="center", children=children)
    top = rem.items[0]
    title_lines = wrap_visual(top.title, line_limit=_TODO_PEEK_LIMIT, max_lines=1)
    title_text = title_lines[0] if title_lines else top.title
    rel = relative_short(top.updated, now=now)
    children += [
        text(f'"{title_text}"', size="body", weight="bold"),
        spacer(),
        text(rel, size="sm", weight="bold", color="blue"),
    ]
    return hstack(gap=8, align="center", children=children)
```

Update the call site inside `build_overview_page`'s children list:

```python
_todos_row(reminders, now=now),
```

- [ ] **Step 4: Run tests, confirm pass**

```bash
cd producer && uv run pytest tests/test_pages_overview.py -v
```

Expected: all overview tests PASS (quote tests + 3 new todo tests).

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/pages/overview.py producer/tests/test_pages_overview.py
git commit -m "feat(overview): peek at top reminder title in TODO row"
```

---

## Task 9: Overview — GitHub velocity row

Replace the `pr_open/pr_review` chunk with `M week · top:{repo} · {streak}d`.

**Files:**
- Modify: `producer/rcpaper/pages/overview.py`
- Modify: `producer/tests/test_pages_overview.py`

- [ ] **Step 1: Append failing tests**

Add to `producer/tests/test_pages_overview.py`:

```python
from rcpaper.sources.github import RepoWeek


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
    assert "3" in texts          # today
    assert "18" in texts         # week
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
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd producer && uv run pytest tests/test_pages_overview.py::test_overview_github_row_shows_week_and_top_repo \
    tests/test_pages_overview.py::test_overview_github_row_no_top_repo_when_quiet_week -v
```

Expected: tests FAIL — current `_github_row` references `gh.pr_open`/`gh.pr_review` (now removed in Task 4), so the page should already be broken; the test asserts the new layout.

- [ ] **Step 3: Rewrite `_github_row` in `pages/overview.py`**

Replace the existing `_github_row` function with:

```python
def _github_row(gh: GitHubActivity | None) -> dict:
    if gh is None:
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("github", size=16, color="black"),
                text("GITHUB", size="sm", weight="bold", color="white", bg="black", pad=4),
                text("—", size="body", weight="bold"),
                spacer(),
            ],
        )
    today_color: ColorToken = "green" if gh.today_commits > 0 else "black"
    week_color: ColorToken = "green" if gh.week_commits > 0 else "black"
    streak_color: ColorToken = (
        "red" if gh.streak >= 7 else "yellow" if gh.streak >= 3 else "green"
    )
    children: list[dict] = [
        icon("github", size=16, color="black"),
        text("GITHUB", size="sm", weight="bold", color="white", bg="black", pad=4),
        text(str(gh.today_commits), size="h2", color=today_color, weight="bold"),
        text("today", size="sm", weight="bold"),
        text(str(gh.week_commits), size="h2", color=week_color, weight="bold"),
        text("week", size="sm", weight="bold"),
    ]
    if gh.repos_week:
        children += [
            text("top:", size="sm"),
            text(gh.repos_week[0].name, size="body", weight="bold"),
        ]
    children += [
        spacer(),
        text(f"{gh.streak}d", size="sm", weight="bold", color=streak_color),
    ]
    return hstack(gap=8, align="center", children=children)
```

Also update the module docstring comment on line 43 from `[pr_open/pr_review PRs]` to `[week count] [top:repo] [streak]`.

- [ ] **Step 4: Run tests, confirm pass**

```bash
cd producer && uv run pytest tests/test_pages_overview.py -v
```

Expected: all overview tests PASS.

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/pages/overview.py producer/tests/test_pages_overview.py
git commit -m "feat(overview): github row shows week+top-repo, drops PR counts"
```

---

## Task 10: Overview — SOON branch for empty-today calendar

When today has no events but upcoming does, show `SOON DOW HH:MM · title` in place of `NEXT CLEAR`.

**Files:**
- Modify: `producer/rcpaper/pages/overview.py`
- Modify: `producer/tests/test_pages_overview.py`

- [ ] **Step 1: Append failing tests**

Add to `producer/tests/test_pages_overview.py`:

```python
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
```

(Add `from rcpaper.sources.calendar import CalEvent` to the test imports if not already present — yes it already is.)

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd producer && uv run pytest tests/test_pages_overview.py::test_overview_next_row_soon_when_today_empty_but_upcoming \
    tests/test_pages_overview.py::test_overview_next_row_nothing_this_week_when_all_empty -v
```

Expected: tests FAIL — `_next_event_row` doesn't read `cal.upcoming`.

- [ ] **Step 3: Update `_next_event_row` in `pages/overview.py`**

Replace the function with:

```python
def _next_event_row(cal: TodaysCalendar | None, now: dt.datetime) -> dict:
    if cal is None:
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("calendar", size=16, color="blue"),
                text("NEXT", size="sm", weight="bold", color="white", bg="blue", pad=4),
                text("—", size="body", weight="bold"),
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
        title = nxt.title
        if len(title) > 24:
            title = title[:23] + "…"
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("calendar", size=16, color="blue"),
                text("NEXT", size="sm", weight="bold", color="white", bg="blue", pad=4),
                text(when, size="body", color="blue", weight="bold"),
                text(title, size="body", weight="bold"),
                spacer(),
                text(sub, size="sm"),
            ],
        )

    if cal.upcoming:
        nxt = cal.upcoming[0]
        dow = nxt.start.strftime("%a").upper()
        when = "ALL" if nxt.is_all_day else nxt.start.strftime("%H:%M")
        title = nxt.title
        if len(title) > 22:
            title = title[:21] + "…"
        return hstack(
            gap=8,
            align="center",
            children=[
                icon("calendar", size=16, color="blue"),
                text("SOON", size="sm", weight="bold", color="white", bg="blue", pad=4),
                text(f"{dow} {when}", size="body", color="blue", weight="bold"),
                text(title, size="body", weight="bold"),
                spacer(),
            ],
        )

    return hstack(
        gap=8,
        align="center",
        children=[
            icon("calendar", size=16, color="blue"),
            text("NEXT", size="sm", weight="bold", color="white", bg="blue", pad=4),
            text("CLEAR", size="body", weight="bold", color="green"),
            spacer(),
            text("nothing this week", size="sm"),
        ],
    )
```

- [ ] **Step 4: Run tests, confirm pass**

```bash
cd producer && uv run pytest tests/test_pages_overview.py -v
```

Expected: all overview tests PASS.

- [ ] **Step 5: Full suite check**

```bash
cd producer && uv run pytest -v
```

Expected: all tests PASS across all source/page files.

- [ ] **Step 6: Commit**

```bash
git add producer/rcpaper/pages/overview.py producer/tests/test_pages_overview.py
git commit -m "feat(overview): SOON row when today is empty but week has events"
```

---

## Acceptance

After all 10 tasks land:

```bash
cd producer && uv run pytest -v
cd producer && uv run ruff format && uv run ruff check --fix
```

Expected: all tests PASS, ruff clean. Manual smoke if a live macOS box is available:

```bash
cd producer && uv run python -m scripts.overview_fetch run    # overview
cd producer && uv run python -m scripts.calendar_fetch run    # calendar
cd producer && uv run python -m scripts.github_fetch run      # github
```

Each should ingest without raising; the device pulls them on its next refresh cycle.
