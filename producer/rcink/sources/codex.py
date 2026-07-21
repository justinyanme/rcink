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
from numbers import Real
from pathlib import Path

import httpx
from loguru import logger

from rcink.timefmt import format_countdown

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


def _reset_from_seconds(secs: float | int | None) -> str:
    if secs is None:
        return ""
    return format_countdown(int(secs))


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

    # API schema (verified 2026-07-20):
    #   rate_limit: { primary_window: {...}, secondary_window: {...} | null }
    # Each window: used_percent (int), reset_after_seconds (int),
    # limit_window_seconds (int), reset_at (epoch).
    rl = data.get("rate_limit")
    if not isinstance(rl, dict):
        raise ValueError("Codex usage response missing/malformed rate_limit")

    def window_block(key: str) -> dict:
        v = rl.get(key)
        if not isinstance(v, dict):
            raise ValueError(f"Codex usage response missing/malformed window: {key}")
        return v

    def optional_window_block(key: str) -> dict | None:
        if key not in rl:
            raise ValueError(f"Codex usage response missing window: {key}")
        return None if rl[key] is None else window_block(key)

    def pct(block: dict) -> float | None:
        v = block.get("used_percent")
        if isinstance(v, bool) or not isinstance(v, Real):
            raise ValueError("Codex usage response has invalid used_percent")
        value = float(v)
        if value < 0.0:
            raise ValueError(f"Codex usage response used_percent out of range: {value}")
        if value > 100.0:
            logger.warning(f"Codex usage response used_percent over 100: {value}; clamping to 100")
            return 100.0
        return value

    def reset(block: dict) -> str:
        secs = block.get("reset_after_seconds")
        if isinstance(secs, bool) or not isinstance(secs, Real):
            raise ValueError("Codex usage response has invalid reset_after_seconds")
        return _reset_from_seconds(secs)

    def reset_at(block: dict) -> int | None:
        # Prefer the absolute epoch the API gives; fall back to now + relative.
        epoch = block.get("reset_at")
        if isinstance(epoch, Real) and not isinstance(epoch, bool):
            return int(epoch)
        secs = block.get("reset_after_seconds")
        if isinstance(secs, Real) and not isinstance(secs, bool):
            return int(dt.datetime.now(dt.UTC).timestamp() + secs)
        raise ValueError("Codex usage response has invalid reset timing")

    def window_min(block: dict) -> int | None:
        secs = block.get("limit_window_seconds")
        if isinstance(secs, bool) or not isinstance(secs, Real):
            raise ValueError("Codex usage response has invalid limit_window_seconds")
        return int(secs) // 60

    primary = window_block("primary_window")
    secondary = optional_window_block("secondary_window")

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
        secondary_pct=pct(secondary) if secondary is not None else None,
        secondary_reset=reset(secondary) if secondary is not None else "",
        secondary_window_min=window_min(secondary) if secondary is not None else None,
        credits_dollars=dollars,
        primary_reset_at=reset_at(primary),
        secondary_reset_at=reset_at(secondary) if secondary is not None else None,
        plan=plan,
    )
