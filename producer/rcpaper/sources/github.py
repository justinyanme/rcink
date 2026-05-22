"""Fetch GitHub activity (commits today, streak, PR counts, contribution grid).

Single GraphQL roundtrip via the v4 API. Returns the dense 14-week heatmap
the producer's github page renders. Token + username come from env:
GITHUB_TOKEN must have `read:user` + `repo` scopes; GITHUB_USERNAME is the
login whose activity we report on.
"""

from __future__ import annotations

import datetime as dt
import os
from dataclasses import dataclass

import httpx

GRAPHQL_URL = "https://api.github.com/graphql"

_QUERY = """
query($user: String!, $reviewQuery: String!) {
  user(login: $user) {
    contributionsCollection {
      contributionCalendar {
        totalContributions
        weeks {
          contributionDays { date contributionCount }
        }
      }
    }
    pullRequests(states: OPEN) { totalCount }
  }
  search(query: $reviewQuery, type: ISSUE) { issueCount }
}
"""

# How many weeks of the heatmap we surface. 14 cols × 7 rows = 98 cells —
# matches the `grid` primitive's default footprint on the page.
GRID_WEEKS = 14
GRID_CELLS = GRID_WEEKS * 7


@dataclass(slots=True)
class GitHubActivity:
    user: str
    today_commits: int
    streak: int  # consecutive contribution days, today-inclusive
    pr_open: int  # PRs the user has open
    pr_review: int  # PRs awaiting the user's review
    total_14w: int  # sum of contributions across the 14-week window
    total_year: int  # totalContributions from the calendar (rolling 12mo)
    contrib: list[int]  # 98 ints in [0, 100], normalised vs window max


def _env(key: str) -> str:
    v = os.environ.get(key)
    if not v:
        raise RuntimeError(f"{key} not set in environment")
    return v


def _normalize(counts: list[int]) -> list[int]:
    """Scale raw counts to 0–100 relative to the window max."""
    if not counts:
        return []
    max_v = max(counts) or 1
    return [min(100, int(round((c / max_v) * 100))) for c in counts]


def _compute_streak(days: list[tuple[str, int]]) -> int:
    """Consecutive contribution days ending today. Grace today if it's 0.

    We allow `today == 0` because the device may render before the user has
    committed yet; in that case the streak reflects "yesterday-anchored"
    counting. Once they push anything today, the streak ticks up.
    """
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


def fetch_github_activity(
    *,
    user: str | None = None,
    token: str | None = None,
    now: dt.datetime | None = None,
) -> GitHubActivity:
    user = user or _env("GITHUB_USERNAME")
    token = token or _env("GITHUB_TOKEN")
    review_query = f"is:open is:pr review-requested:{user}"

    r = httpx.post(
        GRAPHQL_URL,
        json={
            "query": _QUERY,
            "variables": {"user": user, "reviewQuery": review_query},
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

    # Flatten weeks → (date, count). GitHub returns weeks chronological,
    # each week Sun→Sat — exactly the column-major order the grid wants.
    days: list[tuple[str, int]] = []
    for week in cal["weeks"]:
        for d in week["contributionDays"]:
            days.append((d["date"], int(d["contributionCount"])))

    today_commits = days[-1][1] if days else 0
    streak = _compute_streak(days)

    # Last 14 full weeks → 98 cells.
    window_days = days[-GRID_CELLS:]
    window_counts = [c for _, c in window_days]
    total_14w = sum(window_counts)
    contrib = _normalize(window_counts)
    # Pad at the front if the account is younger than 98 days.
    while len(contrib) < GRID_CELLS:
        contrib.insert(0, 0)

    return GitHubActivity(
        user=user,
        today_commits=today_commits,
        streak=streak,
        pr_open=int(u["pullRequests"]["totalCount"]),
        pr_review=int(payload["data"]["search"]["issueCount"]),
        total_14w=total_14w,
        total_year=int(cal["totalContributions"]),
        contrib=contrib,
    )
