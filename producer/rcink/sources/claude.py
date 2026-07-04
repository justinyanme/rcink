"""Read the Claude Code CLI's OAuth credential and fetch usage — strictly read-only.

The producer reuses the credential the interactive ``claude`` CLI already
maintains (macOS Keychain item ``Claude Code-credentials``, or
``~/.claude/.credentials.json`` on Linux) and never modifies it. Refreshing would
rotate the OAuth refresh token and rewrite the Keychain item, which forces the
CLI to relogin. So we use the token as-is and fail fast when it is missing,
expired, or rejected: keep using ``claude`` normally and the producer keeps
working off the freshly-maintained token.
"""

from __future__ import annotations

import datetime as dt
import json
import platform
import subprocess
from dataclasses import dataclass
from numbers import Real
from pathlib import Path

import httpx
from loguru import logger

from rcink.timefmt import format_countdown

CREDS_DEFAULT = Path.home() / ".claude" / ".credentials.json"
KEYCHAIN_SERVICE = "Claude Code-credentials"  # macOS Keychain item name
USAGE_URL = "https://api.anthropic.com/api/oauth/usage"


class ClaudeCredsError(RuntimeError):
    """Claude credential is missing, expired, or rejected — refresh via the `claude` CLI."""


@dataclass(slots=True)
class ClaudeUsage:
    five_hour_pct: float | None
    seven_day_pct: float | None
    opus_pct: float | None
    sonnet_pct: float | None
    five_hour_reset: str  # human-formatted countdown e.g. "2h 14m"
    seven_day_reset: str
    five_hour_reset_at: int | None = None  # absolute epoch; live countdown for the keypoint card
    seven_day_reset_at: int | None = None
    plan: str = "Max"


def _keychain_read() -> dict | None:
    if platform.system() != "Darwin":
        return None
    r = subprocess.run(
        ["security", "find-generic-password", "-s", KEYCHAIN_SERVICE, "-w"],
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        return None
    return json.loads(r.stdout)["claudeAiOauth"]


def load_creds(path: Path = CREDS_DEFAULT) -> dict:
    """Read the CLI's credential: the file if present, else the macOS Keychain."""
    if Path(path).exists():
        return json.loads(Path(path).read_text())["claudeAiOauth"]
    creds = _keychain_read()
    if creds is None:
        raise ClaudeCredsError(
            f"Claude credential not found at {path} or in macOS Keychain "
            f"(service='{KEYCHAIN_SERVICE}'). Log in with the `claude` CLI first."
        )
    return creds


def _reset_text(iso: str | None) -> str:
    if not iso:
        return ""
    when = dt.datetime.fromisoformat(iso.replace("Z", "+00:00"))
    delta = (when - dt.datetime.now(dt.timezone.utc)).total_seconds()
    return format_countdown(int(delta))


def fetch_claude_usage(path: Path = CREDS_DEFAULT) -> ClaudeUsage:
    creds = load_creds(path)
    now_ms = int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000)
    if creds["expiresAt"] <= now_ms:
        raise ClaudeCredsError(
            "Claude access token has expired. The producer never refreshes it "
            "(that would force a `claude` relogin) — use the `claude` CLI to "
            "refresh, then the producer picks up the new token automatically."
        )

    r = httpx.get(
        USAGE_URL,
        headers={
            "Authorization": f"Bearer {creds['accessToken']}",
            "anthropic-beta": "oauth-2025-04-20",
        },
        timeout=30,
    )
    if r.status_code == 401:
        raise ClaudeCredsError(
            "Claude rejected the token (401) — it was likely rotated by the `claude` "
            "CLI. Use the CLI and the producer picks up the new token automatically."
        )
    r.raise_for_status()
    data = r.json()

    def window(key: str, *, required: bool) -> dict | None:
        v = data.get(key)
        if v is None and not required:
            return None
        if not isinstance(v, dict):
            raise ValueError(f"Claude usage response missing/malformed window: {key}")
        return v

    def checked_pct(key: str, *, required: bool = True) -> float | None:
        block = window(key, required=required)
        if block is None:
            return None
        value = block.get("utilization")
        if isinstance(value, bool) or not isinstance(value, Real):
            raise ValueError(f"Claude usage response has invalid pct for {key}")
        pct = float(value)
        if pct < 0.0:
            raise ValueError(f"Claude usage response pct out of range for {key}: {pct}")
        if pct > 100.0:
            logger.warning(f"Claude usage response pct over 100 for {key}: {pct}; clamping to 100")
            return 100.0
        return pct

    def reset(key: str) -> str:
        v = window(key, required=True)
        iso = v.get("resets_at")
        if not isinstance(iso, str) or not iso:
            raise ValueError(f"Claude usage response missing reset for {key}")
        return _reset_text(iso)

    def reset_at(key: str) -> int | None:
        v = window(key, required=True)
        iso = v.get("resets_at")
        if not isinstance(iso, str) or not iso:
            raise ValueError(f"Claude usage response missing reset for {key}")
        return int(dt.datetime.fromisoformat(iso.replace("Z", "+00:00")).timestamp())

    return ClaudeUsage(
        five_hour_pct=checked_pct("five_hour"),
        seven_day_pct=checked_pct("seven_day"),
        opus_pct=checked_pct("seven_day_opus", required=False),
        sonnet_pct=checked_pct("seven_day_sonnet", required=False),
        five_hour_reset=reset("five_hour"),
        seven_day_reset=reset("seven_day"),
        five_hour_reset_at=reset_at("five_hour"),
        seven_day_reset_at=reset_at("seven_day"),
    )
