import json
from pathlib import Path

from tlsr_layout.state import Store, TagState, get_or_create, load, save


def test_roundtrip(tmp_path: Path, monkeypatch):
    monkeypatch.setenv("TLSR_LAYOUT_CONFIG_DIR", str(tmp_path))
    store = load()
    assert store.tags == {}

    tag = get_or_create(store, "AA:BB:CC:DD:EE:FF")
    tag.page_index = 3
    tag.last_data_ver = 0xDEADBEEF
    tag.last_pushed_at = 1715000000.0
    save(store)

    raw = json.loads((tmp_path / "state.json").read_text())
    assert raw["tags"]["AA:BB:CC:DD:EE:FF"]["last_data_ver"] == "0xDEADBEEF"

    reloaded = load()
    assert reloaded.tags["AA:BB:CC:DD:EE:FF"].page_index == 3
    assert reloaded.tags["AA:BB:CC:DD:EE:FF"].last_data_ver == 0xDEADBEEF
    assert reloaded.tags["AA:BB:CC:DD:EE:FF"].last_pushed_at == 1715000000.0


def test_missing_file_returns_empty(tmp_path: Path, monkeypatch):
    monkeypatch.setenv("TLSR_LAYOUT_CONFIG_DIR", str(tmp_path / "missing"))
    assert load().tags == {}


def test_load_accepts_integer_data_ver(tmp_path: Path, monkeypatch):
    monkeypatch.setenv("TLSR_LAYOUT_CONFIG_DIR", str(tmp_path))
    (tmp_path / "state.json").write_text(
        json.dumps(
            {
                "version": 1,
                "tags": {
                    "AA": {
                        "page_index": 1,
                        "last_data_ver": 1234,
                        "last_pushed_at": 1.5,
                    }
                },
            }
        )
    )

    assert load().tags["AA"].last_data_ver == 1234


def test_get_or_create_reuses_existing():
    store = Store(tags={"AA": TagState(page_index=4)})

    tag = get_or_create(store, "AA")

    assert tag.page_index == 4
