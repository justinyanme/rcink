"""Pull today's macOS Reminders and push one page event to Axiom."""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcink.axiom import ingest
from rcink.keypoint import write_card
from rcink.keypoint_cards import todo_card
from rcink.pages.todo import build_todo_page
from rcink.sources.reminders import fetch_todays_reminders

app = typer.Typer(add_completion=False, help=__doc__)


@app.command()
def run() -> None:
    now = dt.datetime.now().astimezone()
    rem = fetch_todays_reminders(now=now)
    logger.info(
        f"reminders: list={rem.list_name!r} visible-pool={len(rem.items)} incomplete-total={rem.incomplete_total}"
    )
    page = build_todo_page(rem, now=now)
    ingest(id="todo", layout=page, order=40)

    write_card(todo_card(rem, now=now))


if __name__ == "__main__":
    app()
