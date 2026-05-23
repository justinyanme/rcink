"""Codex usage source ported from rcpapercolor."""

import datetime as dt
import json
import os
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any

AUTH_DEFAULT = Path.home() / ".codex" / "auth.json"
CODEX_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann"
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


def _request_json(
    url: str,
    *,
    method: str = "GET",
    headers: dict[str, str] | None = None,
    body: dict[str, Any] | None = None,
) -> dict[str, Any]:
    raw = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(
        url,
        data=raw,
        method=method,
        headers={"Content-Type": "application/json", **(headers or {})},
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode(errors="replace")
        raise RuntimeError(f"Codex usage request failed: HTTP {exc.code}: {detail}") from exc


def _load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def _save(path: Path, data: dict[str, Any]) -> None:
    path.write_text(json.dumps(data, indent=2))
    os.chmod(path, 0o600)


def _refresh(refresh_token: str) -> dict[str, Any]:
    return _request_json(
        "https://auth.openai.com/oauth/token",
        method="POST",
        body={
            "grant_type": "refresh_token",
            "refresh_token": refresh_token,
            "client_id": CODEX_CLIENT_ID,
        },
    )


def _format_countdown(seconds: int) -> str:
    if seconds <= 0:
        return "now"
    days, rem = divmod(seconds, 86400)
    hours, rem = divmod(rem, 3600)
    minutes, _ = divmod(rem, 60)
    if days:
        return f"{days}d {hours}h"
    if hours:
        return f"{hours}h {minutes}m"
    return f"{minutes}m"


def _reset_from_seconds(seconds: float | int | None) -> str:
    if seconds is None:
        return ""
    return _format_countdown(int(seconds))


def fetch_codex_usage(path: Path = AUTH_DEFAULT) -> CodexUsage:
    auth = _load(path)
    last = dt.datetime.fromisoformat(auth["last_refresh"].replace("Z", "+00:00"))
    if (dt.datetime.now(dt.UTC) - last).days >= REFRESH_AFTER_DAYS:
        refreshed = _refresh(auth["tokens"]["refresh_token"])
        auth["tokens"] = {
            "access_token": refreshed["access_token"],
            "refresh_token": refreshed["refresh_token"],
            "id_token": refreshed.get("id_token", auth["tokens"].get("id_token", "")),
        }
        auth["last_refresh"] = dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
        _save(path, auth)

    data = _request_json(
        "https://chatgpt.com/backend-api/wham/usage",
        headers={"Authorization": f"Bearer {auth['tokens']['access_token']}"},
    )
    rate_limit = data.get("rate_limit") or {}

    def block(key: str) -> dict[str, Any]:
        value = rate_limit.get(key)
        return value if isinstance(value, dict) else {}

    def pct(value: dict[str, Any]) -> float | None:
        used = value.get("used_percent")
        return float(used) if isinstance(used, int | float) else None

    def window_min(value: dict[str, Any]) -> int | None:
        seconds = value.get("limit_window_seconds")
        return int(seconds) // 60 if isinstance(seconds, int | float) else None

    credits = data.get("credits") or {}
    dollars = None
    if credits.get("has_credits"):
        try:
            dollars = float(credits["balance"])
        except (KeyError, TypeError, ValueError):
            dollars = None

    plan_type = data.get("plan_type")
    plan = plan_type.capitalize() if isinstance(plan_type, str) else "Plus"
    primary = block("primary_window")
    secondary = block("secondary_window")
    return CodexUsage(
        primary_pct=pct(primary),
        primary_reset=_reset_from_seconds(primary.get("reset_after_seconds")),
        primary_window_min=window_min(primary),
        secondary_pct=pct(secondary),
        secondary_reset=_reset_from_seconds(secondary.get("reset_after_seconds")),
        secondary_window_min=window_min(secondary),
        credits_dollars=dollars,
        plan=plan,
    )
