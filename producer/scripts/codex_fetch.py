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


def _usage_metric_id(window_min: int) -> str:
    return f"codex.usage.{window_min}m"


@app.command()
def run() -> None:
    usage = fetch_codex_usage()
    if usage.primary_pct is None or usage.primary_window_min is None:
        raise ValueError("Codex usage missing required primary window")
    secondary_log = f"{usage.secondary_pct}%" if usage.secondary_pct is not None else "NA"
    logger.info(f"codex usage: primary={usage.primary_pct}% secondary={secondary_log}")

    primary_metric_id = _usage_metric_id(usage.primary_window_min)
    spark = sparkline_24h(metric_id=primary_metric_id, field="primary_pct")
    page = build_codex_page(usage, sparkline=spark or None)
    ingest(id="codex", layout=page, order=20)

    ingest_metric(id=primary_metric_id, fields={"primary_pct": usage.primary_pct})
    if usage.secondary_pct is not None:
        if usage.secondary_window_min is None:
            raise ValueError("Codex usage missing secondary window duration")
        ingest_metric(
            id=_usage_metric_id(usage.secondary_window_min),
            fields={"primary_pct": usage.secondary_pct},
        )

    write_card(codex_card(usage, now=dt.datetime.now().astimezone()))


if __name__ == "__main__":
    app()
