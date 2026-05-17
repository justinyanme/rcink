import datetime as dt

import pytest
from rcpaper.axiom import AxiomError, ingest, query_apl


@pytest.fixture
def env(monkeypatch):
    monkeypatch.setenv("AXIOM_TOKEN", "xaat-test")
    monkeypatch.setenv("AXIOM_DATASET", "rcpapercolor")


def test_ingest_posts_event(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        json={"ingested": 1, "failed": 0},
        status_code=200,
    )
    ingest(id="claude", layout={"type": "text", "value": "X"}, order=10)
    req = httpx_mock.get_request()
    assert req.headers["Authorization"] == "Bearer xaat-test"
    assert req.headers["Content-Type"] == "application/json"
    body = req.read()
    import json

    payload = json.loads(body)
    assert isinstance(payload, list) and len(payload) == 1
    ev = payload[0]
    assert ev["kind"] == "page"
    assert ev["id"] == "claude"
    assert ev["order"] == 10
    assert ev["layout"] == {"type": "text", "value": "X"}
    # _time must be ISO8601 UTC
    dt.datetime.fromisoformat(ev["_time"].replace("Z", "+00:00"))


def test_ingest_raises_on_failure(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        json={"ingested": 0, "failed": 1, "failures": [{"error": "bad"}]},
        status_code=200,
    )
    with pytest.raises(AxiomError, match="ingest failed"):
        ingest(id="claude", layout={})


def test_ingest_http_error(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        status_code=401,
        text="unauthorized",
    )
    with pytest.raises(AxiomError, match="401"):
        ingest(id="claude", layout={})


def test_query_apl_returns_rows(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/_apl?format=legacy",
        method="POST",
        json={
            "matches": [
                {"data": {"id": "claude", "value": 5}},
                {"data": {"id": "claude", "value": 7}},
            ]
        },
    )
    rows = query_apl("['rcpapercolor'] | take 2", lookback=dt.timedelta(hours=1))
    assert rows == [{"id": "claude", "value": 5}, {"id": "claude", "value": 7}]


def test_missing_env_raises():
    with pytest.raises(RuntimeError, match="AXIOM_TOKEN"):
        ingest(id="x", layout={})


def test_ingest_metric(httpx_mock, env):
    from rcpaper.axiom import ingest_metric

    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        json={"ingested": 1, "failed": 0},
    )
    ingest_metric(id="claude.usage", fields={"five_hour_pct": 42.0, "seven_day_pct": 68.0})
    import json

    body = json.loads(httpx_mock.get_request().read())
    assert body[0]["kind"] == "metric"
    assert body[0]["five_hour_pct"] == 42.0
    assert body[0]["seven_day_pct"] == 68.0
