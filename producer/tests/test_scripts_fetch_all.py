import json

from rcink.axiom import ingest
from rcink.layout import text, vstack
from scripts import fetch_all


def _static_fetcher(page_id: str, value: str):
    def run() -> None:
        ingest(id=page_id, layout=vstack(children=[text(value)]))

    return run


def test_fetch_all_twice_ingests_static_pages_with_carried_timestamps(httpx_mock, monkeypatch, tmp_path):
    monkeypatch.setenv("AXIOM_TOKEN", "xaat-test")
    monkeypatch.setenv("AXIOM_DATASET", "rcpapercolor")
    state_cache = tmp_path / "rcink" / "pages"
    monkeypatch.setenv("RCINK_PAGE_STATE_CACHE", str(state_cache))
    monkeypatch.setattr(
        fetch_all,
        "FETCHERS",
        [
            ("alpha", _static_fetcher("alpha", "A")),
            ("beta", _static_fetcher("beta", "B")),
        ],
    )
    for _ in range(4):
        httpx_mock.add_response(
            url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
            method="POST",
            json={"ingested": 1, "failed": 0},
        )

    fetch_all.run()
    for page_id, data_ts in (("alpha", 101), ("beta", 202)):
        state_path = state_cache / f"{page_id}.json"
        state = json.loads(state_path.read_text())
        state_path.write_text(json.dumps({**state, "data_ts": data_ts}))

    fetch_all.run()

    events = [json.loads(request.read())[0] for request in httpx_mock.get_requests()]
    assert [(event["id"], event["data_ts"]) for event in events[2:]] == [("alpha", 101), ("beta", 202)]
    assert len(events) == 4
