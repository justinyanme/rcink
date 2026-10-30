import datetime as dt
import json

import pytest
from rcink.axiom import AxiomError, _cached_data_ts, ingest, query_apl
from rcink.layout import text, vstack
from rcink.validate import LayoutValidationError


@pytest.fixture
def env(monkeypatch, tmp_path):
    monkeypatch.setenv("AXIOM_TOKEN", "xaat-test")
    monkeypatch.setenv("AXIOM_DATASET", "rcpapercolor")
    page_state_cache = tmp_path / "rcink" / "pages"
    monkeypatch.setenv("RCINK_PAGE_STATE_CACHE", str(page_state_cache))
    return page_state_cache


def _add_ingest_responses(httpx_mock, *, count: int = 1) -> None:
    for _ in range(count):
        httpx_mock.add_response(
            url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
            method="POST",
            json={"ingested": 1, "failed": 0},
            status_code=200,
        )


def _posted_events(httpx_mock) -> list[dict]:
    return [json.loads(request.read())[0] for request in httpx_mock.get_requests()]


def _event_timestamp(event: dict) -> int:
    return int(dt.datetime.fromisoformat(event["_time"].replace("Z", "+00:00")).timestamp())


def test_ingest_new_page_posts_fresh_event_and_writes_state(httpx_mock, env):
    _add_ingest_responses(httpx_mock)
    ingest(id="claude", layout=vstack(children=[text("X")]), order=10)

    req = httpx_mock.get_request()
    assert req.headers["Authorization"] == "Bearer xaat-test"
    assert req.headers["Content-Type"] == "application/json"
    (event,) = _posted_events(httpx_mock)
    assert event["type"] == "rcpaper_page"
    assert event["id"] == "claude"
    assert event["order"] == 10
    assert json.loads(event["layout_json"]) == {"type": "vstack", "children": [{"type": "text", "value": "X"}]}
    assert event["data_ts"] == _event_timestamp(event)

    state = json.loads((env / "claude.json").read_text())
    assert set(state) == {"layout_hash", "data_ts"}
    assert state["data_ts"] == event["data_ts"]
    assert isinstance(state["layout_hash"], str)
    assert len(state["layout_hash"]) == 64


def test_ingest_changed_page_refreshes_data_timestamp(httpx_mock, env):
    _add_ingest_responses(httpx_mock, count=2)
    ingest(id="claude", layout=vstack(children=[text("before")]))

    state_path = env / "claude.json"
    old_state = json.loads(state_path.read_text())
    state_path.write_text(json.dumps({**old_state, "data_ts": 123}))

    ingest(id="claude", layout=vstack(children=[text("after")]))

    _, changed_event = _posted_events(httpx_mock)
    changed_state = json.loads(state_path.read_text())
    assert changed_event["data_ts"] == _event_timestamp(changed_event)
    assert changed_event["data_ts"] != 123
    assert changed_state["data_ts"] == changed_event["data_ts"]
    assert changed_state["layout_hash"] != old_state["layout_hash"]


def test_changed_page_advances_timestamp_when_fresh_stamp_matches_cached_second(tmp_path):
    state_path = tmp_path / "claude.json"
    state_path.write_text(json.dumps({"layout_hash": "a" * 64, "data_ts": 123}))

    data_ts = _cached_data_ts(state_path, layout_hash="b" * 64, fresh_data_ts=123)

    assert data_ts == 124


def test_ingest_unchanged_page_carries_data_timestamp_forward(httpx_mock, env):
    _add_ingest_responses(httpx_mock, count=2)
    layout = vstack(children=[text("static")])
    ingest(id="claude", layout=layout)

    state_path = env / "claude.json"
    state = json.loads(state_path.read_text())
    state_path.write_text(json.dumps({**state, "data_ts": 123}))

    ingest(id="claude", layout=layout)

    first_event, unchanged_event = _posted_events(httpx_mock)
    assert first_event["data_ts"] == _event_timestamp(first_event)
    assert unchanged_event["data_ts"] == 123
    assert json.loads(state_path.read_text()) == {**state, "data_ts": 123}


@pytest.mark.parametrize(
    "corrupt_state",
    [
        "{not json",
        "{}",
        '{"layout_hash": 42, "data_ts": "yesterday"}',
    ],
)
def test_ingest_corrupt_state_falls_back_to_fresh_stamp_and_self_heals(httpx_mock, env, corrupt_state):
    env.mkdir(parents=True)
    state_path = env / "claude.json"
    state_path.write_text(corrupt_state)
    _add_ingest_responses(httpx_mock)

    ingest(id="claude", layout=vstack(children=[text("X")]))

    (event,) = _posted_events(httpx_mock)
    assert event["data_ts"] == _event_timestamp(event)
    healed = json.loads(state_path.read_text())
    assert set(healed) == {"layout_hash", "data_ts"}
    assert healed["data_ts"] == event["data_ts"]
    assert isinstance(healed["layout_hash"], str)
    assert len(healed["layout_hash"]) == 64


def test_ingest_raises_on_failure(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        json={"ingested": 0, "failed": 1, "failures": [{"error": "bad"}]},
        status_code=200,
    )
    with pytest.raises(AxiomError, match="ingest failed"):
        ingest(id="claude", layout=vstack(children=[]))


def test_ingest_http_error(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        status_code=401,
        text="unauthorized",
    )
    with pytest.raises(AxiomError, match="401"):
        ingest(id="claude", layout=vstack(children=[]))


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
        ingest(id="x", layout=vstack(children=[]))


@pytest.mark.parametrize("page_id", ["../escape", "/tmp/escape", "nested/page"])
def test_ingest_rejects_page_ids_that_escape_state_cache(httpx_mock, env, page_id):
    with pytest.raises(ValueError, match="page id"):
        ingest(id=page_id, layout=vstack(children=[]))
    assert httpx_mock.get_request() is None


def test_ingest_rejects_invalid_layout_before_http(httpx_mock, env):
    with pytest.raises(LayoutValidationError):
        ingest(id="bad", layout={"type": "text", "value": "X"})
    assert httpx_mock.get_request() is None


def test_ingest_metric(httpx_mock, env):
    from rcink.axiom import ingest_metric

    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        json={"ingested": 1, "failed": 0},
    )
    ingest_metric(id="claude.usage", fields={"five_hour_pct": 42.0, "seven_day_pct": 68.0})
    body = json.loads(httpx_mock.get_request().read())
    assert body[0]["type"] == "rcpaper_metric"
    assert body[0]["five_hour_pct"] == 42.0
    assert body[0]["seven_day_pct"] == 68.0
