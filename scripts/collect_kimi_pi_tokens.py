#!/usr/bin/env python3
"""Collect daily and lifetime Kimi token usage from local `pi` JSONL sessions.

Scans `~/.pi/agent/sessions/*/*.jsonl` (configurable via `--sessions-dir`),
strictly whitelists Kimi / Moonshot providers (`kimi-coding`, `kimi`,
`moonshot`, case-insensitive exact or prefix match), tracks per-session
`model_change` provider inheritance, buckets `usage.totalTokens` (falling back
to `input + output + cacheRead + cacheWrite`) by local calendar day, and emits
JSON ready to merge into `mbp16` / `m4` Token Usage collector runs.
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
from collections import defaultdict
from dataclasses import dataclass
from datetime import UTC, datetime, tzinfo
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo

KIMI_EXACT_PROVIDERS = frozenset({"kimi-coding", "kimi", "moonshot"})
KIMI_PREFIXES = ("kimi-", "kimi_", "kimi/", "moonshot-", "moonshot_", "moonshot/")


@dataclass
class CollectionResult:
    days: dict[str, int]
    lifetime: int
    files_scanned: int
    messages_counted: int
    skipped_lines: int


def is_kimi_provider(provider: Any) -> bool:
    """Return True iff `provider` matches the strict Kimi/Moonshot whitelist."""
    if not isinstance(provider, str):
        return False
    normalized = provider.strip().lower()
    if not normalized:
        return False
    return normalized in KIMI_EXACT_PROVIDERS or normalized.startswith(KIMI_PREFIXES)


def _non_negative_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int) and value >= 0:
        return value
    return None


def extract_usage_tokens(usage: Any) -> int:
    """Extract total token count from a pi `usage` dict.

    Prefers positive `totalTokens`, falling back to
    `input + output + cacheRead + cacheWrite` when `totalTokens` is missing or 0.
    """
    if not isinstance(usage, dict):
        return 0
    total = _non_negative_int(usage.get("totalTokens"))
    if total is not None and total > 0:
        return total

    fallback = 0
    for key in ("input", "output", "cacheRead", "cacheWrite"):
        val = _non_negative_int(usage.get(key))
        if val is not None:
            fallback += val
    if fallback > 0:
        return fallback
    return 0


def parse_timestamp(value: Any, tz: tzinfo) -> datetime | None:
    """Parse an ISO-8601 string or Unix epoch (s/ms) into `tz`."""
    if isinstance(value, bool) or value is None:
        return None
    if isinstance(value, (int, float)):
        ts = float(value)
        if ts <= 0:
            return None
        if ts >= 1e11:
            ts /= 1000.0
        try:
            return datetime.fromtimestamp(ts, tz=UTC).astimezone(tz)
        except (OSError, OverflowError, ValueError):
            return None
    if isinstance(value, str):
        raw = value.strip()
        if not raw:
            return None
        if raw.endswith("Z"):
            raw = raw[:-1] + "+00:00"
        try:
            dt = datetime.fromisoformat(raw)
        except ValueError:
            return None
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=tz)
        return dt.astimezone(tz)
    return None


def _extract_model_change_provider(record: dict[str, Any]) -> str | None:
    provider = record.get("provider")
    if isinstance(provider, str) and provider.strip():
        return provider.strip()
    model = record.get("model")
    if isinstance(model, dict):
        nested = model.get("provider")
        if isinstance(nested, str) and nested.strip():
            return nested.strip()
    return None


def parse_session_file(
    path: Path,
    tz: tzinfo,
    daily_totals: dict[str, int],
) -> tuple[int, int]:
    """Parse one `pi` JSONL session file into `daily_totals`.

    Returns `(messages_counted, skipped_lines)`.
    """
    messages_counted = 0
    skipped_lines = 0
    inherited_provider: str | None = None

    try:
        raw_lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return 0, 1

    for line in raw_lines:
        stripped = line.strip()
        if not stripped:
            continue
        try:
            record = json.loads(stripped)
        except json.JSONDecodeError:
            skipped_lines += 1
            continue
        if not isinstance(record, dict):
            skipped_lines += 1
            continue

        rec_type = record.get("type")
        if rec_type == "model_change":
            changed = _extract_model_change_provider(record)
            if changed is not None:
                inherited_provider = changed
            continue

        # Support both nested `{"type": "message", "message": {...}}` and flat assistant records.
        msg: dict[str, Any] | None = None
        if rec_type == "message" and isinstance(record.get("message"), dict):
            msg = record["message"]
        elif isinstance(record.get("role"), str):
            msg = record
        if msg is None:
            continue

        if msg.get("role") != "assistant":
            continue

        explicit_provider = msg.get("provider")
        if not (isinstance(explicit_provider, str) and explicit_provider.strip()):
            explicit_provider = record.get("provider")
        active_provider = (
            explicit_provider.strip()
            if isinstance(explicit_provider, str) and explicit_provider.strip()
            else inherited_provider
        )
        if not is_kimi_provider(active_provider):
            continue

        tokens = extract_usage_tokens(msg.get("usage"))
        if tokens <= 0:
            continue

        dt = parse_timestamp(record.get("timestamp"), tz) or parse_timestamp(
            msg.get("timestamp"), tz
        )
        if dt is None:
            skipped_lines += 1
            continue

        day_key = dt.strftime("%Y-%m-%d")
        daily_totals[day_key] += tokens
        messages_counted += 1

    return messages_counted, skipped_lines


def collect_kimi_tokens(
    sessions_dir: Path,
    tz: tzinfo | None = None,
) -> CollectionResult:
    """Scan `sessions_dir/*/*.jsonl` and aggregate daily + lifetime Kimi tokens."""
    target_tz = tz if tz is not None else datetime.now().astimezone().tzinfo
    assert target_tz is not None

    daily_totals: dict[str, int] = defaultdict(int)
    files_scanned = 0
    messages_counted = 0
    skipped_lines = 0

    if sessions_dir.is_dir():
        for session_file in sorted(sessions_dir.glob("*/*.jsonl")):
            if not session_file.is_file():
                continue
            files_scanned += 1
            counted, skipped = parse_session_file(session_file, target_tz, daily_totals)
            messages_counted += counted
            skipped_lines += skipped

    ordered_days = dict(sorted(daily_totals.items()))
    lifetime = sum(ordered_days.values())
    return CollectionResult(
        days=ordered_days,
        lifetime=lifetime,
        files_scanned=files_scanned,
        messages_counted=messages_counted,
        skipped_lines=skipped_lines,
    )


def format_payload(
    result: CollectionResult,
    source: str,
    tz_name: str,
) -> dict[str, Any]:
    return {
        "provider": "kimi",
        "source": source,
        "timezone": tz_name,
        "days": result.days,
        "lifetime": result.lifetime,
        "stats": {
            "filesScanned": result.files_scanned,
            "messagesCounted": result.messages_counted,
            "skippedLines": result.skipped_lines,
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Aggregate daily Kimi token usage from local pi JSONL sessions."
    )
    parser.add_argument(
        "--sessions-dir",
        type=Path,
        default=Path.home() / ".pi" / "agent" / "sessions",
        help="Path to pi agent sessions directory (default: ~/.pi/agent/sessions)",
    )
    parser.add_argument(
        "--tz",
        default=None,
        help="IANA timezone name for local calendar day bucketing (default: system local TZ)",
    )
    parser.add_argument(
        "--source",
        default=socket.gethostname(),
        help="Source host identifier to include in output JSON",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Optional file path to write JSON output (default: stdout)",
    )
    args = parser.parse_args(argv)

    if args.tz:
        target_tz: tzinfo = ZoneInfo(args.tz)
        tz_name = args.tz
    else:
        local_tz = datetime.now().astimezone().tzinfo
        assert local_tz is not None
        target_tz = local_tz
        tz_name = str(local_tz)

    result = collect_kimi_tokens(args.sessions_dir, tz=target_tz)
    payload = format_payload(result, source=args.source, tz_name=tz_name)
    encoded = json.dumps(payload, indent=2, sort_keys=False) + "\n"

    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    else:
        sys.stdout.write(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
