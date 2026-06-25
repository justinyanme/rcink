"""Read the Codex CLI's OAuth token from ~/.codex/auth.json and fetch usage — read-only.

Mirrors ``sources/claude``: the producer reuses the credential the interactive
``codex`` CLI maintains and never modifies it. Refreshing would rotate the OpenAI
refresh token and rewrite ``auth.json``, which forces the CLI to relogin. So we
use the access token as-is and fail fast when it is missing or rejected: keep
using ``codex`` normally and the producer keeps working off the freshly-maintained
token.
"""

from __future__ import annotations

import datetime as dt
import json
from dataclasses import dataclass
from pathlib import Path

import httpx

AUTH_DEFAULT = Path.home() / ".codex" / "auth.json"
USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"


class CodexCredsError(RuntimeError):
    """Codex credential is missing or rejected — refresh via the `codex` CLI."""


@dataclass(slots=True)
class CodexUsage:
    primary_pct: float | None
    primary_reset: str
    primary_window_min: int | None
    secondary_pct: float | None
    secondary_reset: str
    secondary_window_min: int | None
    credits_dollars: float | None
    primary_reset_at: int | None = None  # absolute epoch; live countdown for the keypoint card
    secondary_reset_at: int | None = None
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


def load_token(path: Path = AUTH_DEFAULT) -> str:
    """Read the CLI's access token from auth.json; raise if missing."""
    try:
        auth = json.loads(Path(path).read_text())
    except FileNotFoundError as e:
        raise CodexCredsError(f"Codex credential not found at {path}. Log in with the `codex` CLI first.") from e
    token = (auth.get("tokens") or {}).get("access_token")
    if not token:
        raise CodexCredsError(f"Codex credential at {path} has no access token. Log in with the `codex` CLI first.")
    return token


def fetch_codex_usage(path: Path = AUTH_DEFAULT) -> CodexUsage:
    token = load_token(path)

    r = httpx.get(USAGE_URL, headers={"Authorization": f"Bearer {token}"}, timeout=30)
    if r.status_code == 401:
        raise CodexCredsError(
            "Codex rejected the token (401) — it was likely rotated by the `codex` "
            "CLI. Use the CLI and the producer picks up the new token automatically."
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

    def reset_at(block: dict) -> int | None:
        # Prefer the absolute epoch the API gives; fall back to now + relative.
        epoch = block.get("reset_at")
        if isinstance(epoch, (int, float)):
            return int(epoch)
        secs = block.get("reset_after_seconds")
        if isinstance(secs, (int, float)):
            return int(dt.datetime.now(dt.timezone.utc).timestamp() + secs)
        return None

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
        primary_reset_at=reset_at(primary),
        secondary_reset_at=reset_at(secondary),
        plan=plan,
    )
