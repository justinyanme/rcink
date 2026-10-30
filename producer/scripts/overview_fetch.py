"""Build the OVERVIEW page from all sources and push to Axiom (order=5).

Runs each fetcher behind a try/except so a single transient outage (e.g.,
expired Codex auth) doesn't blank the whole page — failed blocks render as
dashes.
"""

from __future__ import annotations

import datetime as dt
from collections.abc import Callable

import typer
from loguru import logger
from rcink.axiom import ingest
from rcink.keypoint import write_card
from rcink.keypoint_cards import weather_card
from rcink.pages.overview import WeatherIssue, build_overview_page
from rcink.sources.calendar import fetch_todays_events
from rcink.sources.claude import fetch_claude_usage
from rcink.sources.codex import fetch_codex_usage
from rcink.sources.github import fetch_github_activity
from rcink.sources.quotes import pick_quote
from rcink.sources.reminders import fetch_todays_reminders
from rcink.sources.weather import Weather, WeatherConfigError, fetch_weather

app = typer.Typer(add_completion=False, help=__doc__)


def _try[T](name: str, fn: Callable[[], T]) -> T | None:
    """Run a source fetcher; log+degrade if it raises."""
    try:
        return fn()
    except Exception as e:
        logger.warning(f"overview: {name} fetch failed: {e}")
        return None


def _try_weather() -> tuple[Weather | None, WeatherIssue | None]:
    try:
        return fetch_weather(), None
    except WeatherConfigError as e:
        logger.warning(f"overview: weather config failed: {e}")
        return None, "config"
    except Exception as e:
        logger.warning(f"overview: weather fetch failed: {e}")
        return None, "fetch"


@app.command()
def run() -> None:
    now = dt.datetime.now().astimezone()
    claude = _try("claude", fetch_claude_usage)
    codex = _try("codex", fetch_codex_usage)
    cal = _try("calendar", lambda: fetch_todays_events(now=now))
    rem = _try("reminders", lambda: fetch_todays_reminders(now=now))
    github = _try("github", fetch_github_activity)
    weather, weather_issue = _try_weather()
    # pick_quote is deterministic + offline; quote layout is validated by page
    # tests so oversized curated entries fail before deploy.
    quote = pick_quote(now.date())
    logger.info(
        f"overview: claude={'ok' if claude else 'NA'} "
        f"codex={'ok' if codex else 'NA'} "
        f"calendar={len(cal.events) if cal else 'NA'} "
        f"reminders={rem.incomplete_total if rem else 'NA'} "
        f"github={'ok' if github else 'NA'} "
        f"weather={'ok' if weather else 'NA'}"
    )
    page = build_overview_page(
        claude=claude,
        codex=codex,
        calendar=cal,
        reminders=rem,
        weather=weather,
        weather_issue=weather_issue,
        github=github,
        quote=quote,
        now=now,
    )
    # order=5 → shown first, before claude (10) / codex (20) / calendar (30) / todo (40) / github (50).
    ingest(id="overview", layout=page, order=5)
    if weather is not None:
        write_card(weather_card(weather, now=now))


if __name__ == "__main__":
    app()
