import datetime as dt
import json

import pytest

from rcink.sources.claude import ClaudeCredsError, ClaudeUsage, ScopedLimit
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


@pytest.fixture
def snapshot_dir(monkeypatch, tmp_path):
    monkeypatch.setenv("RCINK_PAGE_STATE_CACHE", str(tmp_path))
    return tmp_path


def _raise_creds_error() -> ClaudeUsage:
    raise ClaudeCredsError("expired")


def test_run_ingests_scoped_limit_metric_fields(monkeypatch, snapshot_dir):
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


def test_run_without_scoped_limits_keeps_base_metric_shape(monkeypatch, snapshot_dir):
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


def test_run_saves_page_snapshot(monkeypatch, snapshot_dir):
    usage = make_usage(scoped_limits=[])
    ingested: list[dict] = []

    monkeypatch.setattr(claude_fetch, "fetch_claude_usage", lambda: usage)
    monkeypatch.setattr(claude_fetch, "sparkline_24h", lambda **_kwargs: [])
    monkeypatch.setattr(claude_fetch, "ingest", lambda **kwargs: ingested.append(kwargs))
    monkeypatch.setattr(claude_fetch, "ingest_metric", lambda **_kwargs: None)
    monkeypatch.setattr(claude_fetch, "write_card", lambda _card: None)

    claude_fetch.run()

    snapshot = json.loads((snapshot_dir / "claude.snapshot.json").read_text())
    assert snapshot["layout"] == ingested[0]["layout"]
    fetched_at = dt.datetime.fromisoformat(snapshot["fetched_at"])
    assert dt.datetime.now(dt.UTC) - fetched_at < dt.timedelta(minutes=1)


def test_creds_outage_reingests_last_page(monkeypatch, snapshot_dir):
    layout = {"type": "vstack", "children": []}
    (snapshot_dir / "claude.snapshot.json").write_text(
        json.dumps({"fetched_at": dt.datetime.now(dt.UTC).isoformat(), "layout": layout})
    )
    ingested: list[dict] = []
    metrics: list[dict] = []
    cards: list[object] = []

    monkeypatch.setattr(claude_fetch, "fetch_claude_usage", _raise_creds_error)
    monkeypatch.setattr(claude_fetch, "ingest", lambda **kwargs: ingested.append(kwargs))
    monkeypatch.setattr(claude_fetch, "ingest_metric", lambda **kwargs: metrics.append(kwargs))
    monkeypatch.setattr(claude_fetch, "write_card", lambda card: cards.append(card))

    claude_fetch.run()

    assert ingested == [{"id": "claude", "layout": layout, "order": 10}]
    assert metrics == []
    assert cards == []


def test_creds_outage_without_snapshot_raises(monkeypatch, snapshot_dir):
    monkeypatch.setattr(claude_fetch, "fetch_claude_usage", _raise_creds_error)

    with pytest.raises(ClaudeCredsError):
        claude_fetch.run()


def test_creds_outage_expired_snapshot_raises(monkeypatch, snapshot_dir):
    stale = dt.datetime.now(dt.UTC) - claude_fetch.SNAPSHOT_MAX_AGE - dt.timedelta(minutes=1)
    (snapshot_dir / "claude.snapshot.json").write_text(
        json.dumps({"fetched_at": stale.isoformat(), "layout": {"type": "vstack"}})
    )
    ingested: list[dict] = []

    monkeypatch.setattr(claude_fetch, "fetch_claude_usage", _raise_creds_error)
    monkeypatch.setattr(claude_fetch, "ingest", lambda **kwargs: ingested.append(kwargs))

    with pytest.raises(ClaudeCredsError):
        claude_fetch.run()
    assert ingested == []


def test_creds_outage_corrupt_snapshot_raises(monkeypatch, snapshot_dir):
    (snapshot_dir / "claude.snapshot.json").write_text("not json")

    monkeypatch.setattr(claude_fetch, "fetch_claude_usage", _raise_creds_error)

    with pytest.raises(ClaudeCredsError):
        claude_fetch.run()
