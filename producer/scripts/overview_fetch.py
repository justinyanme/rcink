"""Build the OVERVIEW page from all sources and push to Axiom (order=5).

Runs each fetcher behind a try/except so a single transient outage (e.g.,
expired Codex auth) doesn't blank the whole page — failed blocks render as
dashes.
"""

from __future__ import annotations

import datetime as dt
from collections.abc import Callable
from typing import TypeVar

import typer
from loguru import logger
from rcpaper.axiom import ingest
from rcpaper.pages.overview import build_overview_page
from rcpaper.sources.calendar import fetch_todays_events
from rcpaper.sources.claude import fetch_claude_usage
from rcpaper.sources.codex import fetch_codex_usage
from rcpaper.sources.github import fetch_github_activity
from rcpaper.sources.quotes import pick_quote
from rcpaper.sources.reminders import fetch_todays_reminders

app = typer.Typer(add_completion=False, help=__doc__)

T = TypeVar("T")


def _try(name: str, fn: Callable[[], T]) -> T | None:
    """Run a source fetcher; log+degrade if it raises."""
    try:
        return fn()
    except Exception as e:  # noqa: BLE001 — log + degrade
        logger.warning(f"overview: {name} fetch failed: {e}")
        return None


@app.command()
def run() -> None:
    now = dt.datetime.now().astimezone()
    claude = _try("claude", fetch_claude_usage)
    codex = _try("codex", fetch_codex_usage)
    cal = _try("calendar", lambda: fetch_todays_events(now=now))
    rem = _try("reminders", lambda: fetch_todays_reminders(now=now))
    github = _try("github", fetch_github_activity)
    # pick_quote is deterministic + offline; never expected to raise, so call
    # it directly. The header drops it gracefully if the joined string is too
    # long to share the row with the date stamp.
    quote = pick_quote(now.date())
    logger.info(
        f"overview: claude={'ok' if claude else 'NA'} "
        f"codex={'ok' if codex else 'NA'} "
        f"calendar={len(cal.events) if cal else 'NA'} "
        f"reminders={rem.incomplete_total if rem else 'NA'} "
        f"github={'ok' if github else 'NA'}"
    )
    page = build_overview_page(
        claude=claude,
        codex=codex,
        calendar=cal,
        reminders=rem,
        github=github,
        quote=quote,
        now=now,
    )
    # order=5 → shown first, before claude (10) / codex (20) / calendar (30) / todo (40) / github (50).
    ingest(id="overview", layout=page, order=5)


if __name__ == "__main__":
    app()
