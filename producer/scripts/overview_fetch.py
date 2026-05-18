"""Build the OVERVIEW page from all sources and push to Axiom (order=5).

Runs each fetcher behind a try/except so a single transient outage (e.g.,
expired Codex auth) doesn't blank the whole page — failed blocks render as
dashes.
"""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcpaper.axiom import ingest
from rcpaper.pages.overview import build_overview_page

app = typer.Typer(add_completion=False, help=__doc__)


def _try_claude():
    from rcpaper.sources.claude import fetch_claude_usage

    try:
        return fetch_claude_usage()
    except Exception as e:  # noqa: BLE001 — log + degrade
        logger.warning(f"overview: claude fetch failed: {e}")
        return None


def _try_codex():
    from rcpaper.sources.codex import fetch_codex_usage

    try:
        return fetch_codex_usage()
    except Exception as e:  # noqa: BLE001
        logger.warning(f"overview: codex fetch failed: {e}")
        return None


def _try_calendar(now: dt.datetime):
    from rcpaper.sources.calendar import fetch_todays_events

    try:
        return fetch_todays_events(now=now)
    except Exception as e:  # noqa: BLE001
        logger.warning(f"overview: calendar fetch failed: {e}")
        return None


def _try_reminders(now: dt.datetime):
    from rcpaper.sources.reminders import fetch_todays_reminders

    try:
        return fetch_todays_reminders(now=now)
    except Exception as e:  # noqa: BLE001
        logger.warning(f"overview: reminders fetch failed: {e}")
        return None


@app.command()
def run() -> None:
    now = dt.datetime.now().astimezone()
    claude = _try_claude()
    codex = _try_codex()
    cal = _try_calendar(now)
    rem = _try_reminders(now)
    logger.info(
        f"overview: claude={'ok' if claude else 'NA'} "
        f"codex={'ok' if codex else 'NA'} "
        f"calendar={len(cal.events) if cal else 'NA'} "
        f"reminders={rem.incomplete_total if rem else 'NA'}"
    )
    page = build_overview_page(
        claude=claude,
        codex=codex,
        calendar=cal,
        reminders=rem,
        now=now,
    )
    # order=5 → shown first, before claude (10) / codex (20) / calendar (30) / todo (40) / quote (80).
    ingest(id="overview", layout=page, order=5)


if __name__ == "__main__":
    app()
