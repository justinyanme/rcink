"""Pull Claude usage and push one page event to Axiom."""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcpaper.axiom import ingest, ingest_metric, query_apl
from rcpaper.pages.claude import build_claude_page
from rcpaper.sources.claude import fetch_claude_usage

app = typer.Typer(add_completion=False, help=__doc__)


def _sparkline_24h() -> list[float]:
    """Last-24h hourly average of the 5h-window usage %.

    The producer runs every 15 min, so even with light usage we get 4 points
    per hour to average. We skip empty bins entirely so the sparkline shows
    actual signal instead of long zero runs.
    """
    rows = query_apl(
        "['rocry'] "
        "| where ['type'] == 'rcpaper_metric' and ['id'] == 'claude.usage' "
        "| summarize avg_pct = avg(['five_hour_pct']) by bin(_time, 1h)",
        lookback=dt.timedelta(hours=24),
    )
    out: list[float] = []
    for r in rows:
        v = r.get("avg_pct")
        if v is not None:
            out.append(float(v))
    return out


@app.command()
def run() -> None:
    usage = fetch_claude_usage()
    logger.info(f"claude usage: 5h={usage.five_hour_pct}% 7d={usage.seven_day_pct}%")

    spark = _sparkline_24h()
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
