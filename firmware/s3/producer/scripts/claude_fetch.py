"""Pull Claude usage and push one page event to Axiom."""

from __future__ import annotations

import typer
from loguru import logger
from rcpaper.axiom import ingest, ingest_metric, sparkline_24h
from rcpaper.pages.claude import build_claude_page
from rcpaper.sources.claude import fetch_claude_usage

app = typer.Typer(add_completion=False, help=__doc__)


@app.command()
def run() -> None:
    usage = fetch_claude_usage()
    logger.info(f"claude usage: 5h={usage.five_hour_pct}% 7d={usage.seven_day_pct}%")

    spark = sparkline_24h(metric_id="claude.usage", field="five_hour_pct")
    page = build_claude_page(usage, sparkline=spark or None)
    ingest(id="claude", layout=page, order=10)

    ingest_metric(
        id="claude.usage",
        fields={
            "five_hour_pct": usage.five_hour_pct or 0,
            "seven_day_pct": usage.seven_day_pct or 0,
        },
    )


if __name__ == "__main__":
    app()
