import datetime as dt

from rcink.sources.codex import CodexUsage
from scripts import codex_fetch


def test_run_publishes_single_window_usage(monkeypatch):
    usage = CodexUsage(
        primary_pct=12.0,
        primary_reset="4d 18h",
        primary_window_min=10080,
        secondary_pct=None,
        secondary_reset="",
        secondary_window_min=None,
        credits_dollars=None,
        primary_reset_at=int(dt.datetime(2026, 7, 25, tzinfo=dt.UTC).timestamp()),
        secondary_reset_at=None,
        plan="Pro",
    )
    ingested: list[dict] = []
    metrics: list[dict] = []
    cards: list[object] = []
    sparkline_queries: list[dict] = []

    def query_sparkline(**kwargs):
        sparkline_queries.append(kwargs)
        return []

    monkeypatch.setattr(codex_fetch, "fetch_codex_usage", lambda: usage)
    monkeypatch.setattr(codex_fetch, "sparkline_24h", query_sparkline)
    monkeypatch.setattr(codex_fetch, "ingest", lambda **kwargs: ingested.append(kwargs))
    monkeypatch.setattr(codex_fetch, "ingest_metric", lambda **kwargs: metrics.append(kwargs))
    monkeypatch.setattr(codex_fetch, "write_card", cards.append)

    codex_fetch.run()

    assert ingested[0]["id"] == "codex"
    assert sparkline_queries == [{"metric_id": "codex.usage.10080m", "field": "primary_pct"}]
    assert metrics == [{"id": "codex.usage.10080m", "fields": {"primary_pct": 12.0}}]
    assert len(cards) == 1


def test_run_partitions_secondary_metric_by_window_duration(monkeypatch):
    usage = CodexUsage(
        primary_pct=42.0,
        primary_reset="2h",
        primary_window_min=300,
        secondary_pct=68.0,
        secondary_reset="3d",
        secondary_window_min=10080,
        credits_dollars=None,
    )
    metrics: list[dict] = []

    monkeypatch.setattr(codex_fetch, "fetch_codex_usage", lambda: usage)
    monkeypatch.setattr(codex_fetch, "sparkline_24h", lambda **_kwargs: [])
    monkeypatch.setattr(codex_fetch, "ingest", lambda **_kwargs: None)
    monkeypatch.setattr(codex_fetch, "ingest_metric", lambda **kwargs: metrics.append(kwargs))
    monkeypatch.setattr(codex_fetch, "write_card", lambda _card: None)

    codex_fetch.run()

    assert metrics == [
        {"id": "codex.usage.300m", "fields": {"primary_pct": 42.0}},
        {"id": "codex.usage.10080m", "fields": {"primary_pct": 68.0}},
    ]
