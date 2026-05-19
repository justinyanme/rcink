"""Pull Codex usage and push one page event to Axiom."""

from __future__ import annotations

import typer
from loguru import logger
from rcpaper.axiom import ingest, ingest_metric, sparkline_24h
from rcpaper.pages.codex import build_codex_page
from rcpaper.sources.codex import fetch_codex_usage

app = typer.Typer(add_completion=False, help=__doc__)


@app.command()
def run() -> None:
    usage = fetch_codex_usage()
    logger.info(f"codex usage: primary={usage.primary_pct}% secondary={usage.secondary_pct}%")

    spark = sparkline_24h(metric_id="codex.usage", field="primary_pct")
    page = build_codex_page(usage, sparkline=spark or None)
    ingest(id="codex", layout=page, order=20)

    ingest_metric(
        id="codex.usage",
        fields={
            "primary_pct": usage.primary_pct or 0,
            "secondary_pct": usage.secondary_pct or 0,
        },
    )


if __name__ == "__main__":
    app()
