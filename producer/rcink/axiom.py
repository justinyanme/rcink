"""Thin Axiom client. Reads AXIOM_TOKEN + AXIOM_DATASET from env."""

from __future__ import annotations

import datetime as dt
import hashlib
import json
import os
from pathlib import Path
from typing import Any

import httpx
from loguru import logger

from rcink.envutil import require_env
from rcink.validate import validate_page

PAGE_STATE_CACHE_ENV = "RCINK_PAGE_STATE_CACHE"
PAGE_STATE_CACHE_DEFAULT = "~/.cache/rcink/pages"


class AxiomError(RuntimeError):
    pass


def _now_iso() -> str:
    return dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def _post_events(events: list[dict[str, Any]]) -> None:
    """POST `events` to the Axiom ingest endpoint; raise on HTTP/ingest failure."""
    token = require_env("AXIOM_TOKEN")
    dataset = require_env("AXIOM_DATASET")
    url = f"https://api.axiom.co/v1/datasets/{dataset}/ingest"
    r = httpx.post(
        url,
        json=events,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        timeout=30,
    )
    if r.status_code != 200:
        raise AxiomError(f"ingest HTTP {r.status_code}: {r.text}")
    body = r.json()
    if body.get("failed", 0):
        raise AxiomError(f"ingest failed: {body.get('failures')}")


def _page_state_path(id: str) -> Path:
    directory = Path(os.environ.get(PAGE_STATE_CACHE_ENV, PAGE_STATE_CACHE_DEFAULT)).expanduser()
    return directory / f"{id}.json"


def _layout_hash(layout: dict[str, Any]) -> str:
    canonical = json.dumps(layout, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
    return hashlib.sha256(canonical.encode()).hexdigest()


def _cached_data_ts(path: Path, *, layout_hash: str, fresh_data_ts: int) -> int:
    try:
        state = json.loads(path.read_text())
        cached_hash = state["layout_hash"]
        cached_data_ts = state["data_ts"]
        if not isinstance(cached_hash, str) or len(cached_hash) != 64:
            raise ValueError("layout_hash must be a 64-character string")
        if type(cached_data_ts) is not int or cached_data_ts < 0:
            raise ValueError("data_ts must be a non-negative integer")
    except FileNotFoundError:
        return fresh_data_ts
    except (json.JSONDecodeError, KeyError, TypeError, UnicodeDecodeError, ValueError) as exc:
        logger.warning(f"discarding corrupt page state {path}: {exc}")
        return fresh_data_ts
    return cached_data_ts if cached_hash == layout_hash else fresh_data_ts


def _write_page_state(path: Path, *, layout_hash: str, data_ts: int) -> None:
    state = json.dumps({"layout_hash": layout_hash, "data_ts": data_ts}, ensure_ascii=False, separators=(",", ":"))
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(state)
    temporary.replace(path)


def ingest(*, id: str, layout: dict[str, Any], order: int | None = None) -> None:
    validate_page(layout)
    now = dt.datetime.now(dt.UTC)
    layout_hash = _layout_hash(layout)
    state_path = _page_state_path(id)
    data_ts = _cached_data_ts(state_path, layout_hash=layout_hash, fresh_data_ts=int(now.timestamp()))
    event: dict[str, Any] = {
        "_time": now.strftime("%Y-%m-%dT%H:%M:%S.%fZ"),
        # Time this page's content last changed. Unchanged layouts carry the
        # previous value so firmware can skip pixel-identical renders.
        "data_ts": data_ts,
        "type": "rcpaper_page",
        "id": id,
        # Stringify the layout tree so Axiom stores it as one opaque field
        # instead of flattening it into layout.children / layout.gap / ...
        # The firmware does its own JSON parse on `layout_json`.
        "layout_json": json.dumps(layout, ensure_ascii=False, separators=(",", ":")),
    }
    if order is not None:
        event["order"] = order
    _post_events([event])
    _write_page_state(state_path, layout_hash=layout_hash, data_ts=data_ts)


def query_apl(apl: str, *, lookback: dt.timedelta = dt.timedelta(days=7)) -> list[dict[str, Any]]:
    """Run an APL query; return a flat list of result rows.

    - Non-aggregation queries (project/take) populate `matches[]`; each row's
      `data` is the column dict.
    - Aggregation queries (summarize) populate `buckets.series[].groups[]`
      instead; the legacy response shape buries the time bin in `startTime`
      and each aggregation's value under `aggregations[]`. We flatten both
      paths into a single list of dicts so callers don't care which kind of
      query they ran.
    """
    token = require_env("AXIOM_TOKEN")
    end = dt.datetime.now(dt.UTC)
    start = end - lookback
    body = {
        "apl": apl,
        "startTime": start.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "endTime": end.strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    r = httpx.post(
        "https://api.axiom.co/v1/datasets/_apl?format=legacy",
        json=body,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        timeout=30,
    )
    if r.status_code != 200:
        raise AxiomError(f"query HTTP {r.status_code}: {r.text}")
    payload = r.json()

    matches = payload.get("matches") or []
    if matches:
        return [m.get("data", {}) for m in matches]

    series = (payload.get("buckets") or {}).get("series") or []
    rows: list[dict[str, Any]] = []
    for bucket in series:
        groups = bucket.get("groups") or []
        if not groups:
            # Preserve empty bins so callers building time series can see the
            # full range — they distinguish "no data" via None values.
            rows.append({"_time": bucket.get("startTime")})
            continue
        for g in groups:
            row: dict[str, Any] = {"_time": bucket.get("startTime")}
            row.update(g.get("group") or {})
            for agg in g.get("aggregations") or []:
                op = agg.get("op")
                if op is not None:
                    row[op] = agg.get("value")
            rows.append(row)
    return rows


def sparkline_24h(*, metric_id: str, field: str) -> list[float]:
    """Last-24h hourly average of `field` for `rcpaper_metric` events with `id`.

    Producer runs every 15 min, so even with light usage we get 4 samples per
    hour to average. Empty bins are dropped — the sparkline shows actual
    signal rather than long zero runs from idle hours.
    """
    rows = query_apl(
        "['rocry'] "
        f"| where ['type'] == 'rcpaper_metric' and ['id'] == '{metric_id}' "
        f"| summarize avg_pct = avg(['{field}']) by bin(_time, 1h)",
        lookback=dt.timedelta(hours=24),
    )
    out: list[float] = []
    for r in rows:
        v = r.get("avg_pct")
        if v is not None:
            out.append(float(v))
    return out


def ingest_metric(*, id: str, fields: dict[str, float | int | str]) -> None:
    """Write a flat metric event (no layout). Used for sparkline aggregation."""
    _post_events(
        [
            {
                "_time": _now_iso(),
                "type": "rcpaper_metric",
                "id": id,
                **fields,
            }
        ]
    )
