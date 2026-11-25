"""Unit tests for scripts/collect_kimi_pi_tokens.py (Issue #25 Seam 5)."""

from __future__ import annotations

import json
import sys
from datetime import UTC, datetime
from pathlib import Path
from zoneinfo import ZoneInfo

# Ensure repo scripts/ is importable regardless of working directory.
SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import collect_kimi_pi_tokens as collector


def _write_jsonl(path: Path, lines: list[str | dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    rendered = [line if isinstance(line, str) else json.dumps(line) for line in lines]
    path.write_text("\n".join(rendered) + "\n", encoding="utf-8")


def test_provider_whitelist_strictly_matches_kimi_and_moonshot() -> None:
    for accepted in (
        "kimi-coding",
        "kimi",
        "KIMI",
        "Kimi-Coding",
        "kimi_pro",
        "kimi/k2p5",
        "moonshot",
        "Moonshot-v1",
        "moonshot_cn",
        "moonshot/kimi-k2",
    ):
        assert collector.is_kimi_provider(accepted), accepted

    for rejected in (
        "",
        "   ",
        None,
        123,
        "claude",
        "anthropic",
        "openai",
        "codex",
        "google",
        "gemini",
        "openrouter",
        "skimir",
        "not-kimi",
    ):
        assert not collector.is_kimi_provider(rejected), repr(rejected)


def test_multi_session_aggregation_model_change_inheritance_and_whitelist(
    tmp_path: Path,
) -> None:
    tz = ZoneInfo("Asia/Shanghai")

    # Session 1: starts with kimi-coding via model_change, then switches to anthropic,
    # then has an explicit Moonshot override, plus user/tool messages that must be ignored.
    _write_jsonl(
        tmp_path / "proj-a" / "sess-1.jsonl",
        [
            {"type": "session", "id": "s1", "timestamp": "2026-11-23T01:00:00Z"},
            {
                "type": "model_change",
                "provider": "kimi-coding",
                "modelId": "k2p5",
                "timestamp": "2026-11-23T01:00:01Z",
            },
            {
                "type": "message",
                "timestamp": "2026-11-23T02:00:00Z",
                "message": {
                    "role": "user",
                    "usage": {"totalTokens": 9999},
                },
            },
            # Inherits kimi-coding from model_change -> counted (totalTokens).
            {
                "type": "message",
                "timestamp": "2026-11-23T02:05:00Z",
                "message": {
                    "role": "assistant",
                    "usage": {
                        "input": 100,
                        "output": 50,
                        "cacheRead": 200,
                        "cacheWrite": 10,
                        "totalTokens": 400,
                    },
                },
            },
            # Fallback to input + output + cacheRead + cacheWrite when totalTokens absent.
            {
                "type": "message",
                "timestamp": "2026-11-23T02:10:00Z",
                "message": {
                    "role": "assistant",
                    "usage": {
                        "input": 120,
                        "output": 80,
                        "cacheRead": 300,
                        "cacheWrite": 50,
                    },
                },
            },
            # Switch session to anthropic -> subsequent inherited message must be ignored.
            {
                "type": "model_change",
                "provider": "anthropic",
                "modelId": "claude-sonnet",
                "timestamp": "2026-11-23T02:20:00Z",
            },
            {
                "type": "message",
                "timestamp": "2026-11-23T02:25:00Z",
                "message": {
                    "role": "assistant",
                    "usage": {"totalTokens": 50000},
                },
            },
            # Explicit non-Kimi provider -> ignored.
            {
                "type": "message",
                "timestamp": "2026-11-23T02:30:00Z",
                "message": {
                    "role": "assistant",
                    "provider": "openai",
                    "usage": {"totalTokens": 60000},
                },
            },
            # Explicit moonshot provider overrides anthropic model_change -> counted.
            {
                "type": "message",
                "timestamp": "2026-11-23T02:35:00Z",
                "message": {
                    "role": "assistant",
                    "provider": "Moonshot",
                    "usage": {"totalTokens": 1000},
                },
            },
        ],
    )

    # Session 2: different project dir, flat record + nested model_change + epoch ms timestamp.
    _write_jsonl(
        tmp_path / "proj-b" / "sess-2.jsonl",
        [
            {
                "type": "model_change",
                "model": {"provider": "kimi"},
                "timestamp": "2026-11-24T04:00:00Z",
            },
            {
                "role": "assistant",
                "timestamp": int(
                    datetime(2026, 11, 24, 4, 0, tzinfo=UTC).timestamp() * 1000
                ),
                "usage": {"totalTokens": 2500},
            },
        ],
    )

    result = collector.collect_kimi_tokens(tmp_path, tz=tz)
    assert result.files_scanned == 2
    assert result.messages_counted == 4
    assert result.skipped_lines == 0
    assert result.days == {
        "2026-11-23": 400 + (120 + 80 + 300 + 50) + 1000,
        "2026-11-24": 2500,
    }
    assert result.lifetime == 4450


def test_local_timezone_day_bucketing_across_midnight_boundary(tmp_path: Path) -> None:
    # 2026-11-23T15:59:59Z is 2026-11-23 23:59:59 in Asia/Shanghai (+08:00).
    # 2026-11-23T16:00:01Z is 2026-11-24 00:00:01 in Asia/Shanghai (+08:00).
    _write_jsonl(
        tmp_path / "proj-tz" / "sess.jsonl",
        [
            {
                "type": "message",
                "timestamp": "2026-11-23T15:59:59Z",
                "message": {
                    "role": "assistant",
                    "provider": "kimi-coding",
                    "usage": {"totalTokens": 111},
                },
            },
            {
                "type": "message",
                "timestamp": "2026-11-23T16:00:01Z",
                "message": {
                    "role": "assistant",
                    "provider": "kimi-coding",
                    "usage": {"totalTokens": 222},
                },
            },
        ],
    )

    shanghai = collector.collect_kimi_tokens(tmp_path, tz=ZoneInfo("Asia/Shanghai"))
    assert shanghai.days == {"2026-11-23": 111, "2026-11-24": 222}

    utc = collector.collect_kimi_tokens(tmp_path, tz=ZoneInfo("UTC"))
    assert utc.days == {"2026-11-23": 333}


def test_malformed_lines_and_corrupt_fields_are_skipped_gracefully(
    tmp_path: Path,
) -> None:
    _write_jsonl(
        tmp_path / "proj-bad" / "mixed.jsonl",
        [
            "",
            "{truncated json",
            "[]",
            json.dumps(
                {
                    "type": "message",
                    "timestamp": "not-a-date",
                    "message": {
                        "role": "assistant",
                        "provider": "kimi-coding",
                        "usage": {"totalTokens": 500},
                    },
                }
            ),
            json.dumps(
                {
                    "type": "message",
                    "timestamp": "2026-11-24T08:00:00Z",
                    "message": {
                        "role": "assistant",
                        "provider": "kimi-coding",
                        "usage": {"totalTokens": -10, "input": "bad", "output": True},
                    },
                }
            ),
            json.dumps(
                {
                    "type": "message",
                    "timestamp": "2026-11-24T08:05:00Z",
                    "message": {
                        "role": "assistant",
                        "provider": "kimi-coding",
                        "usage": {"totalTokens": 0, "input": 30, "output": 70},
                    },
                }
            ),
        ],
    )

    result = collector.collect_kimi_tokens(tmp_path, tz=ZoneInfo("Asia/Shanghai"))
    assert result.files_scanned == 1
    assert result.messages_counted == 1
    assert result.skipped_lines == 3  # truncated json, non-dict [], bad timestamp
    assert result.days == {"2026-11-24": 100}
    assert result.lifetime == 100


def test_cli_outputs_expected_json_shape(tmp_path: Path) -> None:
    _write_jsonl(
        tmp_path / "proj" / "s.jsonl",
        [
            {
                "type": "message",
                "timestamp": "2026-11-24T01:00:00Z",
                "message": {
                    "role": "assistant",
                    "provider": "kimi-coding",
                    "usage": {"totalTokens": 777},
                },
            }
        ],
    )
    out_file = tmp_path / "out.json"
    rc = collector.main(
        [
            "--sessions-dir",
            str(tmp_path),
            "--tz",
            "Asia/Shanghai",
            "--source",
            "mbp16",
            "--output",
            str(out_file),
        ]
    )
    assert rc == 0
    data = json.loads(out_file.read_text(encoding="utf-8"))
    assert data["provider"] == "kimi"
    assert data["source"] == "mbp16"
    assert data["timezone"] == "Asia/Shanghai"
    assert data["days"] == {"2026-11-24": 777}
    assert data["lifetime"] == 777
    assert data["stats"] == {
        "filesScanned": 1,
        "messagesCounted": 1,
        "skippedLines": 0,
    }
