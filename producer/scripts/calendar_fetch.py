"""Pull today's macOS Calendar events and push one page event to Axiom."""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcpaper.axiom import ingest
from rcpaper.pages.calendar import build_calendar_page
from rcpaper.sources.calendar import fetch_todays_events

app = typer.Typer(add_completion=False, help=__doc__)


@app.command()
def run() -> None:
    now = dt.datetime.now().astimezone()
    cal = fetch_todays_events(now=now)
    logger.info(f"calendar: {len(cal.events)} events for {cal.today}")
    page = build_calendar_page(cal, now=now)
    ingest(id="calendar", layout=page, order=30)


if __name__ == "__main__":
    app()
