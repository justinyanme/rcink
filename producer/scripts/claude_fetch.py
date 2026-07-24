"""Pull Claude usage and push one page event to Axiom.

During a creds outage the last good page is re-ingested for up to 24h so it
stays on the device decks; metrics and the keypoint card are not re-emitted.
"""

from __future__ import annotations

import datetime as dt
import json
import os
import re
from pathlib import Path

import typer
from loguru import logger

from rcink.axiom import PAGE_STATE_CACHE_DEFAULT, PAGE_STATE_CACHE_ENV, ingest, ingest_metric, sparkline_24h
from rcink.keypoint import write_card
from rcink.keypoint_cards import claude_card
from rcink.pages.claude import build_claude_page
from rcink.sources.claude import ClaudeCredsError, ClaudeUsage, fetch_claude_usage
from rcink.timefmt import format_countdown

app = typer.Typer(add_completion=False, help=__doc__)

# A creds outage lasts until the user next opens the `claude` CLI — the producer
# must never refresh the shared OAuth token (rotation forces a CLI relogin) —
# so it spans hours while the usage data barely moves. Re-ingesting the last
# good layout verbatim keeps the page inside the firmware's 1h Axiom window,
# and the unchanged layout hash carries `data_ts` forward, so devices show
# honest data age. Past this bound the page drops from the decks as before.
SNAPSHOT_MAX_AGE = dt.timedelta(hours=24)


def _snapshot_path() -> Path:
    directory = Path(os.environ.get(PAGE_STATE_CACHE_ENV, PAGE_STATE_CACHE_DEFAULT)).expanduser()
    return directory / "claude.snapshot.json"


def _save_snapshot(layout: dict) -> None:
    path = _snapshot_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(
        {"fetched_at": dt.datetime.now(dt.UTC).isoformat(), "layout": layout},
        ensure_ascii=False,
    )
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(payload)
    temporary.replace(path)


def _reingest_snapshot(creds_error: ClaudeCredsError) -> None:
    path = _snapshot_path()
    try:
        snapshot = json.loads(path.read_text())
        fetched_at = dt.datetime.fromisoformat(snapshot["fetched_at"])
        layout = snapshot["layout"]
    except FileNotFoundError:
        raise creds_error from None
    except (json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        logger.warning(f"discarding corrupt claude page snapshot {path}: {exc}")
        raise creds_error from None
    age = format_countdown(int((dt.datetime.now(dt.UTC) - fetched_at).total_seconds()))
    if dt.datetime.now(dt.UTC) - fetched_at > SNAPSHOT_MAX_AGE:
        logger.warning(f"claude page snapshot is {age} old; letting the page drop")
        raise creds_error from None
    # Metrics and the keypoint card are not re-emitted: stale samples would
    # falsify the sparkline, and the card's 6h age-out is its staleness signal.
    logger.warning(f"claude creds unavailable; re-ingesting page from {age} ago: {creds_error}")
    ingest(id="claude", layout=layout, order=10)


def _usage_metric_fields(usage: ClaudeUsage) -> dict[str, float]:
    if usage.five_hour_pct is None or usage.seven_day_pct is None:
        raise ValueError("Claude usage missing required five_hour/seven_day pct")
    fields: dict[str, float] = {
        "five_hour_pct": usage.five_hour_pct,
        "seven_day_pct": usage.seven_day_pct,
    }
    for limit in usage.scoped_limits:
        slug = re.sub(r"[^a-z0-9]+", "_", limit.label.lower()).strip("_")
        if not slug:
            raise ValueError(f"Claude scoped limit label cannot form a metric field: {limit.label!r}")
        field = f"{slug}_pct"
        if field in fields:
            raise ValueError(f"duplicate Claude usage metric field: {field}")
        fields[field] = limit.pct
    return fields


@app.command()
def run() -> None:
    try:
        usage = fetch_claude_usage()
    except ClaudeCredsError as creds_error:
        _reingest_snapshot(creds_error)
        return
    metric_fields = _usage_metric_fields(usage)
    logger.info(f"claude usage: 5h={usage.five_hour_pct}% 7d={usage.seven_day_pct}%")

    spark = sparkline_24h(metric_id="claude.usage", field="five_hour_pct")
    page = build_claude_page(usage, sparkline=spark or None)
    ingest(id="claude", layout=page, order=10)
    _save_snapshot(page)

    ingest_metric(
        id="claude.usage",
        fields=metric_fields,
    )

    write_card(claude_card(usage, now=dt.datetime.now().astimezone()))


if __name__ == "__main__":
    app()
