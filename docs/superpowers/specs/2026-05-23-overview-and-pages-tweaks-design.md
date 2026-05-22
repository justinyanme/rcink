# Overview + Pages Tweaks — Design

Date: 2026-05-23
Scope: producer-side layout/source changes only. No firmware changes.

## Goals

Four related tweaks the user surfaced together:

1. Calendar page should roll over to upcoming days when today has no events.
2. GitHub activity is low-signal in both overview and standalone — rebuild around shipping/velocity instead of action-items.
3. Quote on the overview page is too small and truncates aggressively; allow up to 3 lines at a larger size.
4. Reminder row on overview shows only a count — should peek at the top reminder, similar to how the calendar row peeks at the next event.

These are independent feature tweaks bundled in one spec because they share files (`pages/overview.py`, source layer) and the user requested them together.

---

## 1. Calendar — empty-today rollover

### Source — `producer/rcpaper/sources/calendar.py`

Widen the EventKit query window from `[midnight, midnight+1d)` to `[midnight, midnight+7d)`.

Change the dataclass:

```python
@dataclass(slots=True)
class TodaysCalendar:
    today: dt.date
    events: list[CalEvent]            # today only (back-compat for overview NEXT row + page)
    upcoming: list[CalEvent]           # days +1..+6, sorted
```

Where `events` keeps the old semantics (today's events) so existing call sites don't break, and `upcoming` is the new 6-day forward window. Drop the `Weather for ...` filter and the existing sort behavior — apply both to each list.

### Page — `producer/rcpaper/pages/calendar.py`

Three branches:

- **`events` non-empty**: current layout (NEXT hero + UP NEXT list). No change.
- **`events` empty, `upcoming` non-empty**:
  - Compact `NO EVENTS · clear runway` line at top (smaller than the current hero — `body` for `NO EVENTS`, `sm` for "clear runway").
  - `UPCOMING` section title (`sm` bold).
  - Up to 6 rows from `upcoming`, format: `DOW HH:MM · title` where `DOW` is the 3-letter weekday upper (`MON`, `TUE`, …) colored from `_TICK_COLORS` cycle (matches today's UP NEXT styling). All-day events use `ALL` instead of `HH:MM`.
- **`events` empty, `upcoming` empty**: `NOTHING UNTIL NEXT WEEK · clear runway` (single block, `h1` green, like the current `NO EVENTS`).

### Overview row — `_next_event_row` in `pages/overview.py`

Currently shows `NEXT {time} {title}` if today has an upcoming event, else `NEXT CLEAR`.

New:

- Today has upcoming → unchanged.
- Today empty, `upcoming` non-empty → swap pill text to `SOON` (still blue bg), show `{DOW} {HH:MM} · {title}` (truncate title to 22 cells; DOW eats a couple of cells).
- Both empty → keep `NEXT CLEAR` state but change trailing text to `nothing this week`.

---

## 2. GitHub — velocity rebuild

### Source — `producer/rcpaper/sources/github.py`

Add a second aliased `contributionsCollection` to the GraphQL query, scoped to a 7-day window:

```graphql
query($user: String!, $weekFrom: DateTime!, $weekTo: DateTime!) {
  user(login: $user) {
    contributionsCollection {                      # existing — 12mo calendar
      contributionCalendar { totalContributions weeks { ... } }
    }
    week: contributionsCollection(from: $weekFrom, to: $weekTo) {
      commitContributionsByRepository(maxRepositories: 10) {
        repository {
          nameWithOwner
          languages(first: 5, orderBy: {field: SIZE, direction: DESC}) {
            edges { size node { name color } }
          }
        }
        contributions { totalCount }
      }
    }
  }
}
```

Drop `pullRequests(states: OPEN)` and the separate `search` for review-requested issues — those map to the action-items framing the user rejected.

Compute new fields:

```python
@dataclass(slots=True)
class RepoWeek:
    name: str          # nameWithOwner truncated to short repo name
    commits: int

@dataclass(slots=True)
class LangWeek:
    name: str          # e.g. "Go"
    pct: int           # 0..100, normalised across top 5

@dataclass(slots=True)
class GitHubActivity:
    user: str
    today_commits: int
    week_commits: int           # NEW — sum of contributions.totalCount in the 7-day window
    streak: int
    total_14w: int
    total_year: int
    contrib: list[int]          # 98 cells, unchanged
    repos_week: list[RepoWeek]  # NEW — top 5 by commits, desc
    langs_week: list[LangWeek]  # NEW — top 5 languages, commit-weighted, desc, sums to ≤100
    # REMOVED: pr_open, pr_review
```

Language aggregation: for each repo in `repos_week`, multiply each language's `size` byte count by `commits / total_week_commits`, sum across repos, normalize to %, take top 5.

Short repo name: when the repo owner equals `GITHUB_USERNAME`, show the bare `repo` segment (e.g. `rcpapercolor`); otherwise keep `owner/repo` (e.g. `m5stack/M5GFX`). Truncate at 22 visual cells with `…` so the bar always has room.

### Page — `producer/rcpaper/pages/github.py` rewrite

```
GITHUB (h1)                                       SUN · MAY 23
──────────────────────────────────────────────────────────────
@rocry · 7d streak
──────────────────────────────────────────────────────────────
TODAY      WEEK       14W
  3 (h1)    18 (h1)    142 (h1)
COMMITS    COMMITS    TOTAL

REPOS · 7D
  rcpapercolor   ████████   12
  miniomi        ███         5
  obagent        ██          3
  rocpy          █           2
  tlsr-layout    ▏           1

LANGUAGES · 7D
  ████████░░░░░░░░░░░░  Go 45 · Swift 25 · Py 15 · Rust 10 · TS 5

────── CONTRIBUTIONS · 14W ──────             142 TOTAL
[heatmap grid — unchanged]
```

- KPI strip: 3 cells (`TODAY` / `WEEK` / `14W`). Replaces the old `TODAY / STREAK / PRs` strip. Streak moves to the `@user` line.
- Repos section: header `REPOS · 7D` (`sm` bold). Rows are `hstack`s of `text(repo_name, body bold) + hbar(pct=commits/top_commits*100, color=green, height=10) + text(count, sm bold)`. Cap at 5 rows.
- Languages section: header `LANGUAGES · 7D`. If `langs_week` is empty, omit the section entirely. Implementation: a stacked-segment hbar isn't a primitive — fall back to a single full-width hbar plus an inline color-coded text legend `Go 45 · Swift 25 · ...`. Each language label gets a palette color from a fixed cycle (`green / blue / red / yellow / black`) consistent with the rest of the producer.
- Heatmap row + total: unchanged.

If GraphQL returns no week data (new user, quiet week): the KPI strip still works (zeros), repos and languages sections are omitted.

### Overview row — `_github_row` in `pages/overview.py`

Old: `[icon] [GITHUB] N today · Md streak · pr_open/pr_review PRs`

New: `[icon] [GITHUB] N today · M week · top:{repo} · {streak}d`

- `top:` prefix is `sm` bold; repo name is `body` bold to draw the eye.
- Streak stays at the right of the row (color-coded as today).
- Drop the PR ratio entirely from the overview row.

---

## 3. Quote on overview — larger, multi-line

### Page — `pages/overview.py` header restructure

Currently the header is one row: `"inline quote"` (truncated at 30 chars) + spacer + `date_label`. Replace with:

- **Header row**: just the date, right-aligned, `sm` bold.
- **Quote block** (below the header, above the first divider):
  - Use `h2` size, weight bold.
  - Wrap the joined quote into up to 3 lines using a shared wrap helper (see below).
  - Wrap each line in `"` to keep the quote-mark feel: leading `"` on line 1, trailing `"` on the last rendered line.
  - If a 4th line would be needed, truncate the 3rd line with `…`.
  - If the quote is empty/missing, omit the entire block (header row remains, content slides up).

### Shared wrap helper — `pages/_common.py`

Extract the `_cell_width` / `_visual_width` / `_split_two_lines` logic from `pages/todo.py` into `pages/_common.py` and generalize to N lines:

```python
def wrap_visual(text: str, *, line_limit: int, max_lines: int) -> list[str]:
    """Greedy word-wrap by visual cells; CJK-aware. Last line gets … on overflow."""
```

`pages/todo.py` switches to call `wrap_visual(body, line_limit=LINE_LIMIT, max_lines=2)`. The shared helper keeps todo and overview-quote wrapping consistent.

Line budget for the overview quote: panel width − 2×16 pad ≈ 508 px usable. `h2` bold avg ≈ 16 px/cell → ~30 visual cells per line. `LINE_LIMIT_QUOTE = 30`, `max_lines=3` → up to ~90 chars displayed, ~3× the current 30-char cap.

---

## 4. Reminder peek on overview

### Source — small addition

`sources/reminders.py` already sorts `items` by most-recently-updated first. No source change needed; access `rem.items[0]` for the top item.

### Page — `_todos_row` in `pages/overview.py`

Old: `[todo] [TODO] N OPEN ··· list_name`

New:

- `rem.items` non-empty: `[todo] [TODO] N · "{top_title}" ··· {relative_updated}`
  - `top_title` from `rem.items[0].title`, truncated at 24 visual cells via the shared wrap helper.
  - `relative_updated` reuses `_relative_short` from `pages/todo.py` (also extract to `_common.py` since two pages now use it).
- `rem.items` empty: `[todo] [TODO] ALL CLEAR ··· {list_name}` with `ALL CLEAR` in green bold.
- `rem is None`: unchanged (`—` placeholder).

### Calendar row symmetry

Already shows the next event title via `_next_event_row`. Item 1's `SOON` branch covers the empty-today peek. No additional change.

---

## File changes summary

| File | Change |
|------|--------|
| `producer/rcpaper/sources/calendar.py` | Widen window to 7d; split `events` / `upcoming` |
| `producer/rcpaper/sources/github.py` | Aliased week contributions query; new dataclasses; drop PR fields |
| `producer/rcpaper/pages/calendar.py` | Empty-today branch with UPCOMING section |
| `producer/rcpaper/pages/github.py` | Rewrite layout: KPI strip, REPOS, LANGUAGES, heatmap |
| `producer/rcpaper/pages/overview.py` | Header restructure (quote block); github row velocity; todos peek; soon branch |
| `producer/rcpaper/pages/_common.py` | `wrap_visual`, `_relative_short` (extracted from `todo.py`) |
| `producer/rcpaper/pages/todo.py` | Switch to shared helpers |
| `producer/tests/...` | New/updated tests for each change (see Testing) |

## Testing

Existing tests follow `test_pages_<x>.py` / `test_sources_<x>.py` naming. New/updated files:

- `tests/test_pages_calendar.py` (new):
  - empty-today + populated upcoming → UPCOMING section rendered with correct DOW prefixes.
  - empty-today + empty upcoming → "NOTHING UNTIL NEXT WEEK" branch.
  - populated today → unchanged layout (regression test).
- `tests/test_sources_github.py` (extend existing):
  - language weighting math: two repos with known languages and commit counts produce expected percentages.
  - empty week (no commits) → `repos_week == []`, `langs_week == []`.
  - short repo name: owner == username → bare repo; foreign owner → `owner/repo`.
- `tests/test_pages_github.py` (new):
  - REPOS/LANGUAGES sections omitted when empty.
  - hbar widths scale against `top_repo` commits.
- `tests/test_pages_overview.py` (new):
  - Quote block: 1-line, 2-line, 3-line, overflow-truncated cases.
  - Missing quote: block absent, date row floats alone.
  - Todos row: top reminder shown; empty list shows ALL CLEAR.
  - Soon branch: empty today with upcoming events shows `SOON DOW HH:MM · title`.
- `tests/test_pages_common.py` (new):
  - `wrap_visual` round-trips Latin and CJK at expected break points; respects max_lines + ellipsis.
- `tests/test_pages_todo.py` (existing): adjust if extraction of `_relative_short` / wrap helpers changes import paths; behavior unchanged.

All tests should run via `uv run pytest` from `producer/`.

## Out of scope

- Firmware-side rendering changes — text sizes (`h1/h2/body/sm`) and primitives (`hbar`, `grid`, etc.) already support everything this design needs.
- New API integrations beyond the second GraphQL alias.
- True line-counts in GitHub view (rejected as too expensive).
- PR action-items surface (rejected as not velocity).
