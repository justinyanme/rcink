"""Pull GitHub activity and push one page event to Axiom."""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcink.axiom import ingest
from rcink.pages.github import build_github_page
from rcink.sources.github import fetch_github_activity

app = typer.Typer(add_completion=False, help=__doc__)


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


if __name__ == "__main__":
    app()
