import datetime as dt
import json

from rcpaper.sources.codex import CodexUsage, fetch_codex_usage


def write_auth(tmp_path, last_refresh_days_ago: float):
    p = tmp_path / "auth.json"
    last = dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=last_refresh_days_ago)
    p.write_text(
        json.dumps(
            {
                "tokens": {
                    "access_token": "old-access",
                    "refresh_token": "rrr",
                    "id_token": "iii",
                },
                "last_refresh": last.strftime("%Y-%m-%dT%H:%M:%SZ"),
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


def test_fresh_auth_skips_refresh(httpx_mock, tmp_path):
    p = write_auth(tmp_path, last_refresh_days_ago=1)
    httpx_mock.add_response(
        url="https://chatgpt.com/backend-api/wham/usage",
        json=_usage_payload(),
    )
    usage = fetch_codex_usage(p)
    assert isinstance(usage, CodexUsage)
    assert usage.primary_pct == 42.0
    assert usage.secondary_pct == 68.0
    assert usage.credits_dollars == 18.42
    assert usage.primary_window_min == 300
    assert usage.secondary_window_min == 10080
    req = httpx_mock.get_request()
    assert req.headers["Authorization"] == "Bearer old-access"


def test_stale_auth_triggers_refresh(httpx_mock, tmp_path):
    p = write_auth(tmp_path, last_refresh_days_ago=9)
    httpx_mock.add_response(
        url="https://auth.openai.com/oauth/token",
        method="POST",
        json={"access_token": "new-access", "refresh_token": "new-rrr", "id_token": "new-iii", "expires_in": 3600},
    )
    httpx_mock.add_response(
        url="https://chatgpt.com/backend-api/wham/usage",
        json=_usage_payload(primary_pct=1.0, secondary_pct=None, has_credits=False, balance="0"),
    )
    fetch_codex_usage(p)
    on_disk = json.loads(p.read_text())
    assert on_disk["tokens"]["access_token"] == "new-access"
    assert on_disk["tokens"]["refresh_token"] == "new-rrr"
