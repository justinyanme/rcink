"""Thin Axiom client. Reads AXIOM_TOKEN + AXIOM_DATASET from env."""

from __future__ import annotations

import datetime as dt
import json
import os
from typing import Any

import httpx


class AxiomError(RuntimeError):
    pass


def _env(key: str) -> str:
    v = os.environ.get(key)
    if not v:
        raise RuntimeError(f"{key} not set in environment")
    return v


def _now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def ingest(*, id: str, layout: dict[str, Any], order: int | None = None) -> None:
    token = _env("AXIOM_TOKEN")
    dataset = _env("AXIOM_DATASET")
    now = dt.datetime.now(dt.timezone.utc)
    event = {
        "_time": now.strftime("%Y-%m-%dT%H:%M:%S.%fZ"),
        # Producer-side wallclock at build time, exposed as epoch seconds so the
        # firmware can format both "data HH:MM" (this value) and
        # "refresh HH:MM" (its own last_fetch_s) without re-parsing _time.
        "data_ts": int(now.timestamp()),
        "type": "rcpaper_page",
        "id": id,
        # Stringify the layout tree so Axiom stores it as one opaque field
        # instead of flattening it into layout.children / layout.gap / ...
        # The firmware does its own JSON parse on `layout_json`.
        "layout_json": json.dumps(layout, ensure_ascii=False, separators=(",", ":")),
    }
    if order is not None:
        event["order"] = order
    url = f"https://api.axiom.co/v1/datasets/{dataset}/ingest"
    r = httpx.post(
        url,
        json=[event],
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        timeout=30,
    )
    if r.status_code != 200:
        raise AxiomError(f"ingest HTTP {r.status_code}: {r.text}")
    body = r.json()
    if body.get("failed", 0):
        raise AxiomError(f"ingest failed: {body.get('failures')}")


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
    token = _env("AXIOM_TOKEN")
    end = dt.datetime.now(dt.timezone.utc)
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


def ingest_metric(*, id: str, fields: dict[str, float | int | str]) -> None:
    """Write a flat metric event (no layout). Used for sparkline aggregation."""
    token = _env("AXIOM_TOKEN")
    dataset = _env("AXIOM_DATASET")
    event = {
        "_time": _now_iso(),
        "type": "rcpaper_metric",
        "id": id,
        **fields,
    }
    url = f"https://api.axiom.co/v1/datasets/{dataset}/ingest"
    r = httpx.post(
        url,
        json=[event],
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        timeout=30,
    )
    if r.status_code != 200:
        raise AxiomError(f"ingest HTTP {r.status_code}: {r.text}")
