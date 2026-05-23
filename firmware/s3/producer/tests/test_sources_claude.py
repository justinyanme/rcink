import json
import time

from rcpaper.sources.claude import (
    ClaudeUsage,
    fetch_claude_usage,
    load_creds,
    save_creds,
)

FRESH = {
    "claudeAiOauth": {
        "accessToken": "sk-ant-oat01-fresh",
        "refreshToken": "sk-ant-ort01-r",
        "expiresAt": int((time.time() + 3600) * 1000),
    }
}

NEAR_EXPIRY = {
    "claudeAiOauth": {
        "accessToken": "sk-ant-oat01-old",
        "refreshToken": "sk-ant-ort01-r",
        "expiresAt": int((time.time() + 60) * 1000),  # 1 min out
    }
}


def write_creds(tmp_path, data):
    p = tmp_path / ".credentials.json"
    p.write_text(json.dumps(data))
    return p


def test_load_and_save_roundtrip(tmp_path):
    p = write_creds(tmp_path, FRESH)
    got = load_creds(p)
    assert got["accessToken"] == "sk-ant-oat01-fresh"
    save_creds(p, {"accessToken": "x", "refreshToken": "y", "expiresAt": 1})
    on_disk = json.loads(p.read_text())
    assert on_disk["claudeAiOauth"]["accessToken"] == "x"


def test_fresh_token_used_directly(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url="https://api.anthropic.com/api/oauth/usage",
        json={
            "five_hour": {"utilization": 42.0, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
            "seven_day_opus": {"utilization": 18.0, "resets_at": "2026-05-24T00:00:00Z"},
            "seven_day_sonnet": {"utilization": 24.0, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    usage = fetch_claude_usage(p)
    assert isinstance(usage, ClaudeUsage)
    assert usage.five_hour_pct == 42.0
    assert usage.seven_day_pct == 68.0
    assert usage.opus_pct == 18.0
    assert usage.sonnet_pct == 24.0
    req = httpx_mock.get_request()
    assert req.headers["Authorization"] == "Bearer sk-ant-oat01-fresh"
    assert req.headers["anthropic-beta"] == "oauth-2025-04-20"


def test_near_expiry_triggers_refresh(httpx_mock, tmp_path):
    p = write_creds(tmp_path, NEAR_EXPIRY)
    # Refresh
    httpx_mock.add_response(
        url="https://console.anthropic.com/v1/oauth/token",
        method="POST",
        json={
            "access_token": "sk-ant-oat01-new",
            "refresh_token": "sk-ant-ort01-new",
            "expires_in": 3600,
        },
    )
    # Usage
    httpx_mock.add_response(
        url="https://api.anthropic.com/api/oauth/usage",
        json={
            "five_hour": {"utilization": 1, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 2, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    fetch_claude_usage(p)
    # File should be updated
    on_disk = json.loads(p.read_text())["claudeAiOauth"]
    assert on_disk["accessToken"] == "sk-ant-oat01-new"
    assert on_disk["refreshToken"] == "sk-ant-ort01-new"
    assert on_disk["expiresAt"] > int(time.time() * 1000) + 3500_000


def test_usage_reset_text_formatting(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    # ~2h 14m into the future from now
    future = int(time.time()) + 2 * 3600 + 14 * 60
    import datetime as dt

    iso = dt.datetime.fromtimestamp(future, tz=dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    httpx_mock.add_response(
        url="https://api.anthropic.com/api/oauth/usage",
        json={
            "five_hour": {"utilization": 42.0, "resets_at": iso},
            "seven_day": {"utilization": 68.0, "resets_at": iso},
        },
    )
    usage = fetch_claude_usage(p)
    assert usage.five_hour_reset.startswith("2h")
