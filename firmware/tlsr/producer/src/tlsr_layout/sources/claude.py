"""Claude usage source ported from rcpapercolor."""

import datetime as dt
import json
import os
import platform
import subprocess
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any

CREDS_DEFAULT = Path.home() / ".claude" / ".credentials.json"
KEYCHAIN_SERVICE = "Claude Code-credentials"
OAUTH_CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
REFRESH_LEEWAY_SEC = 300


@dataclass(slots=True)
class ClaudeUsage:
    five_hour_pct: float | None
    seven_day_pct: float | None
    opus_pct: float | None
    sonnet_pct: float | None
    five_hour_reset: str
    seven_day_reset: str
    plan: str = "Max"


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
        raise RuntimeError(f"Claude usage request failed: HTTP {exc.code}: {detail}") from exc


def _keychain_read() -> dict[str, Any] | None:
    if platform.system() != "Darwin":
        return None
    result = subprocess.run(
        ["security", "find-generic-password", "-s", KEYCHAIN_SERVICE, "-w"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None
    return json.loads(result.stdout)["claudeAiOauth"]


def _keychain_write(creds: dict[str, Any]) -> None:
    payload = json.dumps({"claudeAiOauth": creds})
    subprocess.run(
        [
            "security",
            "add-generic-password",
            "-s",
            KEYCHAIN_SERVICE,
            "-a",
            os.environ.get("USER", ""),
            "-w",
            payload,
            "-U",
        ],
        check=True,
    )


def load_creds(path: Path = CREDS_DEFAULT) -> dict[str, Any]:
    if path.exists():
        return json.loads(path.read_text())["claudeAiOauth"]
    creds = _keychain_read()
    if creds is None:
        raise FileNotFoundError(
            f"Claude credentials not found at {path} or in macOS Keychain "
            f"(service={KEYCHAIN_SERVICE!r}). Log in with Claude CLI first."
        )
    return creds


def save_creds(path: Path, creds: dict[str, Any]) -> None:
    if path.exists() or platform.system() != "Darwin":
        path.write_text(json.dumps({"claudeAiOauth": creds}, indent=2))
        os.chmod(path, 0o600)
        return
    _keychain_write(creds)


def _refresh(refresh_token: str) -> dict[str, Any]:
    body = _request_json(
        "https://console.anthropic.com/v1/oauth/token",
        method="POST",
        body={
            "grant_type": "refresh_token",
            "refresh_token": refresh_token,
            "client_id": OAUTH_CLIENT_ID,
        },
    )
    return {
        "accessToken": body["access_token"],
        "refreshToken": body["refresh_token"],
        "expiresAt": int((dt.datetime.now(dt.UTC).timestamp() + body["expires_in"]) * 1000),
    }


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


def _reset_text(iso: str | None) -> str:
    if not iso:
        return ""
    when = dt.datetime.fromisoformat(iso.replace("Z", "+00:00"))
    delta = int((when - dt.datetime.now(dt.UTC)).total_seconds())
    return _format_countdown(delta)


def fetch_claude_usage(path: Path = CREDS_DEFAULT) -> ClaudeUsage:
    creds = load_creds(path)
    now_ms = int(dt.datetime.now(dt.UTC).timestamp() * 1000)
    if int(creds["expiresAt"]) - now_ms < REFRESH_LEEWAY_SEC * 1000:
        creds = _refresh(str(creds["refreshToken"]))
        save_creds(path, creds)

    data = _request_json(
        "https://api.anthropic.com/api/oauth/usage",
        headers={
            "Authorization": f"Bearer {creds['accessToken']}",
            "anthropic-beta": "oauth-2025-04-20",
        },
    )

    def pct(key: str) -> float | None:
        value = data.get(key)
        return float(value["utilization"]) if isinstance(value, dict) else None

    def reset(key: str) -> str:
        value = data.get(key)
        return _reset_text(value.get("resets_at")) if isinstance(value, dict) else ""

    return ClaudeUsage(
        five_hour_pct=pct("five_hour"),
        seven_day_pct=pct("seven_day"),
        opus_pct=pct("seven_day_opus"),
        sonnet_pct=pct("seven_day_sonnet"),
        five_hour_reset=reset("five_hour"),
        seven_day_reset=reset("seven_day"),
    )
