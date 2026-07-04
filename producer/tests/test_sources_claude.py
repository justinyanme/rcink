import datetime as dt
import json
import time

import pytest
from rcink.sources import claude as claude_mod
from rcink.sources.claude import (
    USAGE_URL,
    ClaudeCredsError,
    ClaudeUsage,
    fetch_claude_usage,
    load_creds,
)

FRESH = {
    "claudeAiOauth": {
        "accessToken": "sk-ant-oat01-fresh",
        "refreshToken": "sk-ant-ort01-r",
        "expiresAt": int((time.time() + 3600) * 1000),
    }
}

EXPIRED = {
    "claudeAiOauth": {
        "accessToken": "sk-ant-oat01-old",
        "refreshToken": "sk-ant-ort01-r",
        "expiresAt": int((time.time() - 60) * 1000),  # 1 min ago
    }
}


def write_creds(tmp_path, data):
    p = tmp_path / ".credentials.json"
    p.write_text(json.dumps(data))
    return p


def test_load_creds_from_file(tmp_path):
    p = write_creds(tmp_path, FRESH)
    assert load_creds(p)["accessToken"] == "sk-ant-oat01-fresh"


def test_load_creds_missing_raises(monkeypatch, tmp_path):
    # No file, and no Keychain fallback -> clear error, no silent empty.
    monkeypatch.setattr(claude_mod, "_keychain_read", lambda: None)
    with pytest.raises(ClaudeCredsError):
        load_creds(tmp_path / "absent.json")


def test_fresh_token_used_read_only(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url=USAGE_URL,
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
    # Absolute reset epoch parsed for the keypoint live countdown.
    assert usage.five_hour_reset_at == int(dt.datetime(2026, 5, 17, 20, 0, tzinfo=dt.timezone.utc).timestamp())
    # Exactly one request (no refresh round-trip), with the token verbatim.
    req = httpx_mock.get_request()
    assert req.headers["Authorization"] == "Bearer sk-ant-oat01-fresh"
    assert req.headers["anthropic-beta"] == "oauth-2025-04-20"


def test_expired_token_raises_without_network(httpx_mock, tmp_path):
    # Expired token must never trigger a refresh; fail fast before any request.
    p = write_creds(tmp_path, EXPIRED)
    with pytest.raises(ClaudeCredsError):
        fetch_claude_usage(p)
    assert not httpx_mock.get_requests()


def test_rejected_token_raises_creds_error(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(url=USAGE_URL, status_code=401, json={"error": "unauthorized"})
    with pytest.raises(ClaudeCredsError):
        fetch_claude_usage(p)


def test_usage_reset_text_formatting(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    # ~2h 14m into the future from now
    future = int(time.time()) + 2 * 3600 + 14 * 60
    iso = dt.datetime.fromtimestamp(future, tz=dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": 42.0, "resets_at": iso},
            "seven_day": {"utilization": 68.0, "resets_at": iso},
        },
    )
    usage = fetch_claude_usage(p)
    assert usage.five_hour_reset.startswith("2h")


def test_missing_required_window_raises(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    with pytest.raises(ValueError, match="five_hour"):
        fetch_claude_usage(p)


def test_pct_out_of_range_raises(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": 142.0, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    with pytest.raises(ValueError, match="out of range"):
        fetch_claude_usage(p)
