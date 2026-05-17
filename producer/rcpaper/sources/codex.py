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


def _reset_text(iso: str | None) -> str:
    if not iso:
        return ""
    when = dt.datetime.fromisoformat(iso.replace("Z", "+00:00"))
    delta = (when - dt.datetime.now(dt.timezone.utc)).total_seconds()
    return _format_countdown(int(delta))


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

    rl = data.get("rate_limits", {})

    def pct(key: str) -> float | None:
        v = rl.get(key)
        return float(v["used_percent"]) if isinstance(v, dict) else None

    def reset(key: str) -> str:
        v = rl.get(key)
        return _reset_text(v.get("resets_at")) if isinstance(v, dict) else ""

    def window(key: str) -> int | None:
        v = rl.get(key)
        return int(v["window_minutes"]) if isinstance(v, dict) and "window_minutes" in v else None

    credits = data.get("credits", {})
    cents = credits.get("balance_cents")
    dollars = float(cents) / 100 if isinstance(cents, (int, float)) else None

    return CodexUsage(
        primary_pct=pct("primary"),
        primary_reset=reset("primary"),
        primary_window_min=window("primary"),
        secondary_pct=pct("secondary"),
        secondary_reset=reset("secondary"),
        secondary_window_min=window("secondary"),
        credits_dollars=dollars,
    )
