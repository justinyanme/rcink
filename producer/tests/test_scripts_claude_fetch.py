import datetime as dt

from rcink.sources.claude import ClaudeUsage, ScopedLimit
from scripts import claude_fetch


def make_usage(*, scoped_limits: list[ScopedLimit]) -> ClaudeUsage:
    return ClaudeUsage(
        five_hour_pct=42.0,
        seven_day_pct=68.0,
        scoped_limits=scoped_limits,
        five_hour_reset="2h",
        seven_day_reset="3d",
        five_hour_reset_at=int(dt.datetime(2026, 7, 25, tzinfo=dt.UTC).timestamp()),
        seven_day_reset_at=int(dt.datetime(2026, 7, 27, tzinfo=dt.UTC).timestamp()),
    )


def test_run_ingests_scoped_limit_metric_fields(monkeypatch):
    usage = make_usage(scoped_limits=[ScopedLimit(label="FABLE", pct=41.0)])
    metrics: list[dict] = []

    monkeypatch.setattr(claude_fetch, "fetch_claude_usage", lambda: usage)
    monkeypatch.setattr(claude_fetch, "sparkline_24h", lambda **_kwargs: [])
    monkeypatch.setattr(claude_fetch, "ingest", lambda **_kwargs: None)
    monkeypatch.setattr(claude_fetch, "ingest_metric", lambda **kwargs: metrics.append(kwargs))
    monkeypatch.setattr(claude_fetch, "write_card", lambda _card: None)

    claude_fetch.run()

    assert metrics == [
        {
            "id": "claude.usage",
            "fields": {
                "five_hour_pct": 42.0,
                "seven_day_pct": 68.0,
                "fable_pct": 41.0,
            },
        }
    ]


def test_run_without_scoped_limits_keeps_base_metric_shape(monkeypatch):
    usage = make_usage(scoped_limits=[])
    metrics: list[dict] = []

    monkeypatch.setattr(claude_fetch, "fetch_claude_usage", lambda: usage)
    monkeypatch.setattr(claude_fetch, "sparkline_24h", lambda **_kwargs: [])
    monkeypatch.setattr(claude_fetch, "ingest", lambda **_kwargs: None)
    monkeypatch.setattr(claude_fetch, "ingest_metric", lambda **kwargs: metrics.append(kwargs))
    monkeypatch.setattr(claude_fetch, "write_card", lambda _card: None)

    claude_fetch.run()

    assert metrics[0]["fields"] == {
        "five_hour_pct": 42.0,
        "seven_day_pct": 68.0,
    }
