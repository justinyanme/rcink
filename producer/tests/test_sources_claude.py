import datetime as dt
import json
import time

import pytest
from rcink.sources import claude as claude_mod
from rcink.sources.claude import (
    USAGE_URL,
    ClaudeCredsError,
    ClaudeUsage,
    ScopedLimit,
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
            "seven_day_opus": None,
            "seven_day_sonnet": None,
            "limits": [
                {
                    "kind": "weekly_scoped",
                    "group": "weekly",
                    "percent": 41,
                    "scope": {
                        "model": {"id": None, "display_name": "Fable"},
                        "surface": None,
                    },
                    "is_active": False,  # Undocumented; scoped limit must still be retained.
                }
            ],
        },
    )
    usage = fetch_claude_usage(p)
    assert isinstance(usage, ClaudeUsage)
    assert usage.five_hour_pct == 42.0
    assert usage.seven_day_pct == 68.0
    assert usage.scoped_limits == [ScopedLimit(label="FABLE", pct=41.0)]
    # Absolute reset epoch parsed for the keypoint live countdown.
    assert usage.five_hour_reset_at == int(dt.datetime(2026, 5, 17, 20, 0, tzinfo=dt.UTC).timestamp())
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
    iso = dt.datetime.fromtimestamp(future, tz=dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": 42.0, "resets_at": iso},
            "seven_day": {"utilization": 68.0, "resets_at": iso},
        },
    )
    usage = fetch_claude_usage(p)
    assert usage.five_hour_reset.startswith("2h")


def test_missing_reset_degrades_to_empty_text_and_no_epoch(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": 42.0},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    usage = fetch_claude_usage(p)
    assert usage.five_hour_pct == 42.0
    assert usage.five_hour_reset == ""
    assert usage.five_hour_reset_at is None


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


def test_pct_over_100_clamps(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": 142.0, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    usage = fetch_claude_usage(p)
    assert usage.five_hour_pct == 100.0


def test_negative_pct_raises(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": -1.0, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    with pytest.raises(ValueError, match="out of range"):
        fetch_claude_usage(p)


def test_missing_limits_warns_and_returns_empty_list(httpx_mock, monkeypatch, tmp_path):
    p = write_creds(tmp_path, FRESH)
    warnings = []
    monkeypatch.setattr(claude_mod.logger, "warning", warnings.append)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": 42.0, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    usage = fetch_claude_usage(p)
    assert usage.scoped_limits == []
    assert any("limits" in warning for warning in warnings)


def test_non_model_and_surface_scoped_limits_are_skipped(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": 42.0, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
            "limits": [
                {
                    "kind": "session",
                    "percent": 42,
                    "scope": {"model": None, "surface": None},
                },
                {
                    "kind": "weekly_all",
                    "percent": 68,
                    "scope": {"model": None, "surface": None},
                },
                {
                    "kind": "weekly_scoped",
                    "percent": 12,
                    "scope": {
                        "model": {"display_name": "Ignored"},
                        "surface": {"display_name": "API"},
                    },
                },
            ],
        },
    )

    assert fetch_claude_usage(p).scoped_limits == []


@pytest.mark.parametrize(
    ("entry", "message"),
    [
        (
            {
                "percent": "41",
                "scope": {"model": {"display_name": "Fable"}, "surface": None},
            },
            "invalid pct",
        ),
        (
            {
                "percent": True,
                "scope": {"model": {"display_name": "Fable"}, "surface": None},
            },
            "invalid pct",
        ),
        (
            {
                "percent": -1,
                "scope": {"model": {"display_name": "Fable"}, "surface": None},
            },
            "out of range",
        ),
        (
            {
                "percent": 41,
                "scope": {"model": {"display_name": ""}, "surface": None},
            },
            "display_name",
        ),
    ],
)
def test_malformed_model_scoped_limit_raises(httpx_mock, tmp_path, entry, message):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": 42.0, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
            "limits": [entry],
        },
    )

    with pytest.raises(ValueError, match=message):
        fetch_claude_usage(p)


def test_scoped_pct_over_100_clamps_with_warning(httpx_mock, monkeypatch, tmp_path):
    p = write_creds(tmp_path, FRESH)
    warnings = []
    monkeypatch.setattr(claude_mod.logger, "warning", warnings.append)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": 42.0, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
            "limits": [
                {
                    "percent": 142,
                    "scope": {"model": {"display_name": "Fable"}, "surface": None},
                }
            ],
        },
    )

    assert fetch_claude_usage(p).scoped_limits == [ScopedLimit(label="FABLE", pct=100.0)]
    assert any("FABLE" in warning and "clamping" in warning for warning in warnings)


def test_bool_pct_raises(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url=USAGE_URL,
        json={
            "five_hour": {"utilization": True, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    with pytest.raises(ValueError, match="invalid pct"):
        fetch_claude_usage(p)
