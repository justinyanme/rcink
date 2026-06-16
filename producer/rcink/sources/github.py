"""Fetch GitHub activity (commits today, week breakdown, streak, daily chart).

Multiple small GraphQL queries:
  - local-day commit aggregates (drives today/week/14w/sparkline)
  - the rolling contributionCalendar (drives existing streak behavior)
  - the rolling 7-day commitContributionsByRepository (visible repo detail only)

GITHUB_TOKEN needs `read:user`; GITHUB_USERNAME is the login to report on.
"""

from __future__ import annotations

import datetime as dt
import os
from dataclasses import dataclass, field

import httpx

GRAPHQL_URL = "https://api.github.com/graphql"

_BASE_QUERY = """
query($user: String!, $weekFrom: DateTime!, $weekTo: DateTime!) {
  user(login: $user) {
    year: contributionsCollection {
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

_DAY_QUERY_TEMPLATE = """
query($user: String!, __DAY_VARS__) {
  user(login: $user) {
    __DAY_FIELDS__
  }
}
"""

# 14 cols × 7 rows = 98 cells — matches the device-side grid primitive footprint.
GRID_WEEKS = 14
GRID_CELLS = GRID_WEEKS * 7
DAY_QUERY_BATCH = 28

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
    daily_counts: list[int]
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
    return _compute_streak_counts([count for _date, count in days])


def _compute_streak_counts(counts: list[int]) -> int:
    if not counts:
        return 0
    i = len(counts) - 1
    streak = 0
    today_zero_grace = counts[i] == 0
    if today_zero_grace:
        i -= 1
    while i >= 0 and counts[i] > 0:
        streak += 1
        i -= 1
    return streak


def _iso_utc(value: dt.datetime) -> str:
    return value.astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _local_day_windows(
    now: dt.datetime,
    *,
    days: int = GRID_CELLS,
) -> list[tuple[dt.datetime, dt.datetime]]:
    local_now = now.astimezone()
    today_start = local_now.replace(hour=0, minute=0, second=0, microsecond=0)
    first_start = today_start - dt.timedelta(days=days - 1)
    windows: list[tuple[dt.datetime, dt.datetime]] = []
    for i in range(days):
        start = first_start + dt.timedelta(days=i)
        end = start + dt.timedelta(days=1)
        if i == days - 1:
            end = local_now
        if end <= start:
            end = start + dt.timedelta(seconds=1)
        windows.append((start, end))
    return windows


def _query_for_day_windows(
    windows: list[tuple[dt.datetime, dt.datetime]],
    *,
    offset: int = 0,
) -> tuple[str, dict[str, str]]:
    var_defs: list[str] = []
    fields: list[str] = []
    variables: dict[str, str] = {}
    for local_i, (start, end) in enumerate(windows):
        i = offset + local_i
        alias = f"d{i:03d}"
        from_var = f"{alias}From"
        to_var = f"{alias}To"
        var_defs.append(f"${from_var}: DateTime!")
        var_defs.append(f"${to_var}: DateTime!")
        fields.append(
            f"{alias}: contributionsCollection(from: ${from_var}, to: ${to_var}) "
            "{ totalCommitContributions restrictedContributionsCount }"
        )
        variables[from_var] = _iso_utc(start)
        variables[to_var] = _iso_utc(end)
    query = _DAY_QUERY_TEMPLATE.replace("__DAY_VARS__", ", ".join(var_defs)).replace(
        "__DAY_FIELDS__",
        "\n    ".join(fields),
    )
    return query, variables


def _post_graphql(*, query: str, variables: dict[str, str], token: str) -> dict:
    r = httpx.post(
        GRAPHQL_URL,
        json={"query": query, "variables": variables},
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
    return payload


def _commit_count(collection: dict) -> int:
    return int(collection["totalCommitContributions"]) + int(collection["restrictedContributionsCount"])


def _fetch_daily_counts(
    *,
    user: str,
    token: str,
    windows: list[tuple[dt.datetime, dt.datetime]],
) -> list[int]:
    counts = [0] * len(windows)
    for offset in range(0, len(windows), DAY_QUERY_BATCH):
        batch = windows[offset : offset + DAY_QUERY_BATCH]
        query, day_variables = _query_for_day_windows(batch, offset=offset)
        payload = _post_graphql(
            query=query,
            variables={"user": user, **day_variables},
            token=token,
        )
        u = payload["data"]["user"]
        if u is None:
            raise RuntimeError(f"GitHub user not found: {user}")
        for i in range(offset, min(offset + DAY_QUERY_BATCH, len(windows))):
            counts[i] = _commit_count(u[f"d{i:03d}"])
    return counts


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
    windows = _local_day_windows(now)
    week_from = _iso_utc(windows[-7][0])
    week_to = _iso_utc(now)

    payload = _post_graphql(
        query=_BASE_QUERY,
        variables={"user": user, "weekFrom": week_from, "weekTo": week_to},
        token=token,
    )

    u = payload["data"]["user"]
    if u is None:
        raise RuntimeError(f"GitHub user not found: {user}")

    cal = u["year"]["contributionCalendar"]
    daily_counts = _fetch_daily_counts(user=user, token=token, windows=windows)
    year_days: list[tuple[str, int]] = []
    for week in cal["weeks"]:
        for day in week["contributionDays"]:
            year_days.append((day["date"], int(day["contributionCount"])))
    today_commits = daily_counts[-1] if daily_counts else 0
    week_commits = sum(daily_counts[-7:])
    streak = _compute_streak(year_days)
    total_14w = sum(daily_counts)

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
    repos_week = [RepoWeek(name=n, commits=c) for n, c, _ in raw_repos[:TOP_REPOS]]
    langs_week = _aggregate_languages(raw_repos[:TOP_REPOS])

    return GitHubActivity(
        user=user,
        today_commits=today_commits,
        week_commits=week_commits,
        streak=streak,
        total_14w=total_14w,
        total_year=int(cal["totalContributions"]),
        daily_counts=daily_counts,
        repos_week=repos_week,
        langs_week=langs_week,
    )
