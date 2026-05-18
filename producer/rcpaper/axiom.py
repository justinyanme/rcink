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


def ingest(*, id: str, layout: dict[str, Any], order: int | None = None, ttl_sec: int = 3600) -> None:
    token = _env("AXIOM_TOKEN")
    dataset = _env("AXIOM_DATASET")
    event = {
        "_time": _now_iso(),
        "type": "rcpaper_page",
        "id": id,
        "ttl_sec": ttl_sec,
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
    """Run an APL query; return a flat list of `data` dicts from matches."""
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
    return [m.get("data", {}) for m in payload.get("matches", [])]


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
