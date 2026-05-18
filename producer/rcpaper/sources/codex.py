"""Read Codex OAuth from ~/.codex/auth.json, refresh + fetch usage."""

from __future__ import annotations

import datetime as dt
import json
import os
from dataclasses import dataclass
from pathlib import Path

import httpx

AUTH_DEFAULT = Path.home() / ".codex" / "auth.json"
CODEX_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann"  # public, from Codex CLI
REFRESH_AFTER_DAYS = 8


@dataclass(slots=True)
class CodexUsage:
    primary_pct: float | None
    primary_reset: str
    primary_window_min: int | None
    secondary_pct: float | None
    secondary_reset: str
    secondary_window_min: int | None
    credits_dollars: float | None
    plan: str = "Plus"


def _format_countdown(secs: int) -> str:
    if secs <= 0:
        return "now"
    d, rem = divmod(secs, 86400)
    h, rem = divmod(rem, 3600)
    m, _ = divmod(rem, 60)
    if d:
        return f"{d}d {h}h"
    if h:
        return f"{h}h {m}m"
    return f"{m}m"


def _reset_from_seconds(secs: float | int | None) -> str:
    if secs is None:
        return ""
    return _format_countdown(int(secs))


def _load(path: Path) -> dict:
    return json.loads(Path(path).read_text())


def _save(path: Path, data: dict) -> None:
    Path(path).write_text(json.dumps(data, indent=2))
    os.chmod(path, 0o600)


def _refresh(refresh_token: str) -> dict:
    r = httpx.post(
        "https://auth.openai.com/oauth/token",
        json={
            "grant_type": "refresh_token",
            "refresh_token": refresh_token,
            "client_id": CODEX_CLIENT_ID,
        },
        timeout=30,
    )
    r.raise_for_status()
    return r.json()


def fetch_codex_usage(path: Path = AUTH_DEFAULT) -> CodexUsage:
    auth = _load(path)
    last = dt.datetime.fromisoformat(auth["last_refresh"].replace("Z", "+00:00"))
    if (dt.datetime.now(dt.timezone.utc) - last).days >= REFRESH_AFTER_DAYS:
        new = _refresh(auth["tokens"]["refresh_token"])
        auth["tokens"] = {
            "access_token": new["access_token"],
            "refresh_token": new["refresh_token"],
            "id_token": new.get("id_token", auth["tokens"].get("id_token", "")),
        }
        auth["last_refresh"] = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        _save(path, auth)

    r = httpx.get(
        "https://chatgpt.com/backend-api/wham/usage",
        headers={"Authorization": f"Bearer {auth['tokens']['access_token']}"},
        timeout=30,
    )
    r.raise_for_status()
    data = r.json()

    # API schema (verified 2026-05-18):
    #   rate_limit: { primary_window: {...}, secondary_window: {...} }
    # Each window: used_percent (int), reset_after_seconds (int),
    # limit_window_seconds (int), reset_at (epoch).
    rl = data.get("rate_limit") or {}

    def window_block(key: str) -> dict:
        v = rl.get(key)
        return v if isinstance(v, dict) else {}

    def pct(block: dict) -> float | None:
        v = block.get("used_percent")
        return float(v) if isinstance(v, (int, float)) else None

    def reset(block: dict) -> str:
        return _reset_from_seconds(block.get("reset_after_seconds"))

    def window_min(block: dict) -> int | None:
        secs = block.get("limit_window_seconds")
        return int(secs) // 60 if isinstance(secs, (int, float)) else None

    primary = window_block("primary_window")
    secondary = window_block("secondary_window")

    # credits.balance is a string ("0", "12.34", ...). Only surface it when
    # `has_credits` is true to avoid showing $0.00 on plans that don't use it.
    credits = data.get("credits") or {}
    dollars: float | None = None
    if credits.get("has_credits"):
        bal = credits.get("balance")
        try:
            dollars = float(bal) if bal is not None else None
        except (TypeError, ValueError):
            dollars = None

    plan_type = data.get("plan_type")
    plan = plan_type.capitalize() if isinstance(plan_type, str) else "Plus"

    return CodexUsage(
        primary_pct=pct(primary),
        primary_reset=reset(primary),
        primary_window_min=window_min(primary),
        secondary_pct=pct(secondary),
        secondary_reset=reset(secondary),
        secondary_window_min=window_min(secondary),
        credits_dollars=dollars,
        plan=plan,
    )
