"""Pull Claude usage and push one page event to Axiom."""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcpaper.axiom import ingest, ingest_metric, query_apl
from rcpaper.pages.claude import build_claude_page
from rcpaper.sources.claude import fetch_claude_usage

app = typer.Typer(add_completion=False, help=__doc__)


def _sparkline_7d() -> list[float]:
    rows = query_apl(
        "['rcpapercolor'] "
        "| where ['kind'] == 'metric' and ['id'] == 'claude.usage' "
        "| summarize avg_pct = avg(['five_hour_pct']) by bin(_time, 1d) "
        "| order by _time asc",
        lookback=dt.timedelta(days=7),
    )
    return [float(r.get("avg_pct", 0) or 0) for r in rows]


@app.command()
def run() -> None:
    usage = fetch_claude_usage()
    logger.info(f"claude usage: 5h={usage.five_hour_pct}% 7d={usage.seven_day_pct}%")

    spark = _sparkline_7d()
    page = build_claude_page(usage, sparkline=spark or None, updated_at=dt.datetime.now())
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
