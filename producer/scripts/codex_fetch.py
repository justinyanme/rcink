"""Pull Codex usage and push one page event to Axiom."""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcpaper.axiom import ingest, ingest_metric, query_apl
from rcpaper.pages.codex import build_codex_page
from rcpaper.sources.codex import fetch_codex_usage

app = typer.Typer(add_completion=False, help=__doc__)


def _sparkline_7d() -> list[float]:
    rows = query_apl(
        "['rcpapercolor'] "
        "| where ['kind'] == 'metric' and ['id'] == 'codex.usage' "
        "| summarize avg_pct = avg(['primary_pct']) by bin(_time, 1d) "
        "| order by _time asc",
        lookback=dt.timedelta(days=7),
    )
    return [float(r.get("avg_pct", 0) or 0) for r in rows]


@app.command()
def run() -> None:
    usage = fetch_codex_usage()
    logger.info(f"codex usage: primary={usage.primary_pct}% secondary={usage.secondary_pct}%")

    spark = _sparkline_7d()
    page = build_codex_page(usage, sparkline=spark or None, updated_at=dt.datetime.now())
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
