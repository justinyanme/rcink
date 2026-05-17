"""Read Claude OAuth from ~/.claude/.credentials.json, refresh + fetch usage."""

from __future__ import annotations

import datetime as dt
import json
import os
from dataclasses import dataclass
from pathlib import Path

import httpx

CREDS_DEFAULT = Path.home() / ".claude" / ".credentials.json"
OAUTH_CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"  # public, from eInkClaude
REFRESH_LEEWAY_SEC = 300  # refresh if expiry < 5 min away


@dataclass(slots=True)
class ClaudeUsage:
    five_hour_pct: float | None
    seven_day_pct: float | None
    opus_pct: float | None
    sonnet_pct: float | None
    five_hour_reset: str  # human-formatted countdown e.g. "2h 14m"
    seven_day_reset: str
    plan: str = "Max"


def load_creds(path: Path = CREDS_DEFAULT) -> dict:
    raw = json.loads(Path(path).read_text())
    return raw["claudeAiOauth"]


def save_creds(path: Path, creds: dict) -> None:
    Path(path).write_text(json.dumps({"claudeAiOauth": creds}, indent=2))
    os.chmod(path, 0o600)


def _refresh(refresh_token: str) -> dict:
    r = httpx.post(
        "https://console.anthropic.com/v1/oauth/token",
        json={
            "grant_type": "refresh_token",
            "refresh_token": refresh_token,
            "client_id": OAUTH_CLIENT_ID,
        },
        timeout=30,
    )
    r.raise_for_status()
    body = r.json()
    return {
        "accessToken": body["access_token"],
        "refreshToken": body["refresh_token"],
        "expiresAt": int((dt.datetime.now(dt.timezone.utc).timestamp() + body["expires_in"]) * 1000),
    }


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


def fetch_claude_usage(path: Path = CREDS_DEFAULT) -> ClaudeUsage:
    creds = load_creds(path)
    now_ms = int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000)
    if creds["expiresAt"] - now_ms < REFRESH_LEEWAY_SEC * 1000:
        creds = _refresh(creds["refreshToken"])
        save_creds(path, creds)

    r = httpx.get(
        "https://api.anthropic.com/api/oauth/usage",
        headers={
            "Authorization": f"Bearer {creds['accessToken']}",
            "anthropic-beta": "oauth-2025-04-20",
        },
        timeout=30,
    )
    r.raise_for_status()
    data = r.json()

    def pct(key: str) -> float | None:
        v = data.get(key)
        return float(v["utilization"]) if isinstance(v, dict) else None

    def reset(key: str) -> str:
        v = data.get(key)
        return _reset_text(v.get("resets_at")) if isinstance(v, dict) else ""

    return ClaudeUsage(
        five_hour_pct=pct("five_hour"),
        seven_day_pct=pct("seven_day"),
        opus_pct=pct("seven_day_opus"),
        sonnet_pct=pct("seven_day_sonnet"),
        five_hour_reset=reset("five_hour"),
        seven_day_reset=reset("seven_day"),
    )
