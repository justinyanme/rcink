import datetime as dt
import json

import pytest
from rcink.sources.codex import USAGE_URL, CodexCredsError, CodexUsage, fetch_codex_usage


def write_auth(tmp_path, access_token: str = "old-access"):
    p = tmp_path / "auth.json"
    p.write_text(
        json.dumps(
            {
                "tokens": {
                    "access_token": access_token,
                    "refresh_token": "rrr",
                    "id_token": "iii",
                },
                "last_refresh": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            }
        )
    )
    return p


def _usage_payload(
    primary_pct: float = 42.0,
    secondary_pct: float | None = 68.0,
    has_credits: bool = True,
    balance: str = "18.42",
    plan: str = "Plus",
) -> dict:
    """Build a response body matching the chatgpt.com/backend-api/wham/usage schema."""
    rate_limit: dict = {
        "primary_window": {
            "used_percent": primary_pct,
            "limit_window_seconds": 18000,  # 5h
            "reset_after_seconds": 7200,  # 2h
        },
    }
    if secondary_pct is not None:
        rate_limit["secondary_window"] = {
            "used_percent": secondary_pct,
            "limit_window_seconds": 604800,  # 7d
            "reset_after_seconds": 86400 * 3,
        }
    return {
        "plan_type": plan.lower(),
        "rate_limit": rate_limit,
        "credits": {"has_credits": has_credits, "balance": balance},
    }


def test_token_used_read_only(httpx_mock, tmp_path):
    p = write_auth(tmp_path)
    httpx_mock.add_response(url=USAGE_URL, json=_usage_payload())
    usage = fetch_codex_usage(p)
    assert isinstance(usage, CodexUsage)
    assert usage.primary_pct == 42.0
    assert usage.secondary_pct == 68.0
    assert usage.credits_dollars == 18.42
    assert usage.primary_window_min == 300
    assert usage.secondary_window_min == 10080
    # Relative reset_after_seconds (7200) folded into an absolute epoch ~2h out.
    assert usage.primary_reset_at is not None
    assert 7100 <= usage.primary_reset_at - int(dt.datetime.now(dt.timezone.utc).timestamp()) <= 7200
    # Exactly one request (no refresh round-trip), with the token verbatim.
    req = httpx_mock.get_request()
    assert req.headers["Authorization"] == "Bearer old-access"


def test_missing_auth_raises(tmp_path):
    with pytest.raises(CodexCredsError):
        fetch_codex_usage(tmp_path / "absent.json")


def test_no_access_token_raises(tmp_path):
    p = tmp_path / "auth.json"
    p.write_text(json.dumps({"tokens": {}}))
    with pytest.raises(CodexCredsError):
        fetch_codex_usage(p)


def test_rejected_token_raises_creds_error(httpx_mock, tmp_path):
    p = write_auth(tmp_path)
    httpx_mock.add_response(url=USAGE_URL, status_code=401, json={"error": "unauthorized"})
    with pytest.raises(CodexCredsError):
        fetch_codex_usage(p)
