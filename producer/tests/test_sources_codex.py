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


def test_fresh_auth_skips_refresh(httpx_mock, tmp_path):
    p = write_auth(tmp_path, last_refresh_days_ago=1)
    httpx_mock.add_response(
        url="https://chatgpt.com/backend-api/wham/usage",
        json={
            "rate_limits": {
                "primary": {"used_percent": 42.0, "resets_at": "2026-05-17T20:00:00Z", "window_minutes": 300},
                "secondary": {"used_percent": 68.0, "resets_at": "2026-05-24T00:00:00Z", "window_minutes": 10080},
            },
            "credits": {"balance_cents": 1842},
        },
    )
    usage = fetch_codex_usage(p)
    assert isinstance(usage, CodexUsage)
    assert usage.primary_pct == 42.0
    assert usage.secondary_pct == 68.0
    assert usage.credits_dollars == 18.42
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
        json={
            "rate_limits": {
                "primary": {"used_percent": 1.0, "resets_at": "2026-05-17T20:00:00Z", "window_minutes": 300}
            },
            "credits": {"balance_cents": 0},
        },
    )
    fetch_codex_usage(p)
    on_disk = json.loads(p.read_text())
    assert on_disk["tokens"]["access_token"] == "new-access"
    assert on_disk["tokens"]["refresh_token"] == "new-rrr"
