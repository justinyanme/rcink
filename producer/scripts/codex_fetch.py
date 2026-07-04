"""Pull Codex usage and push one page event to Axiom."""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcink.axiom import ingest, ingest_metric, sparkline_24h
from rcink.keypoint import write_card
from rcink.keypoint_cards import codex_card
from rcink.pages.codex import build_codex_page
from rcink.sources.codex import fetch_codex_usage

app = typer.Typer(add_completion=False, help=__doc__)


@app.command()
def run() -> None:
    usage = fetch_codex_usage()
    if usage.primary_pct is None or usage.secondary_pct is None:
        raise ValueError("Codex usage missing required primary/secondary pct")
    logger.info(f"codex usage: primary={usage.primary_pct}% secondary={usage.secondary_pct}%")

    spark = sparkline_24h(metric_id="codex.usage", field="primary_pct")
    page = build_codex_page(usage, sparkline=spark or None)
    ingest(id="codex", layout=page, order=20)

    ingest_metric(
        id="codex.usage",
        fields={
            "primary_pct": usage.primary_pct,
            "secondary_pct": usage.secondary_pct,
        },
    )

    write_card(codex_card(usage, now=dt.datetime.now().astimezone()))


if __name__ == "__main__":
    app()
