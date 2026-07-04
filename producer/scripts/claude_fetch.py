"""Pull Claude usage and push one page event to Axiom."""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcink.axiom import ingest, ingest_metric, sparkline_24h
from rcink.keypoint import write_card
from rcink.keypoint_cards import claude_card
from rcink.pages.claude import build_claude_page
from rcink.sources.claude import fetch_claude_usage

app = typer.Typer(add_completion=False, help=__doc__)


@app.command()
def run() -> None:
    usage = fetch_claude_usage()
    if usage.five_hour_pct is None or usage.seven_day_pct is None:
        raise ValueError("Claude usage missing required five_hour/seven_day pct")
    logger.info(f"claude usage: 5h={usage.five_hour_pct}% 7d={usage.seven_day_pct}%")

    spark = sparkline_24h(metric_id="claude.usage", field="five_hour_pct")
    page = build_claude_page(usage, sparkline=spark or None)
    ingest(id="claude", layout=page, order=10)

    ingest_metric(
        id="claude.usage",
        fields={
            "five_hour_pct": usage.five_hour_pct,
            "seven_day_pct": usage.seven_day_pct,
        },
    )

    write_card(claude_card(usage, now=dt.datetime.now().astimezone()))


if __name__ == "__main__":
    app()
