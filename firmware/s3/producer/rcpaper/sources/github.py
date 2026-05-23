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
    name: str  # short display name (see _short_repo_name)
    commits: int


@dataclass(slots=True)
class LangWeek:
    name: str  # e.g. "Go", "Python"
    pct: int  # 0..100, top-5 normalised to sum ≤ 100


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
            (edge["node"]["name"], int(edge["size"])) for edge in (bucket["repository"]["languages"]["edges"] or [])
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
