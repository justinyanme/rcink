from tlsr_layout.ble.adv_parser import Advertisement
from tlsr_layout.encode import encode_page
from tlsr_layout.serve import _maybe_push, _resolve_rotation
from tlsr_layout.state import Store, TagState

from .test_serve_decides_to_push import FakeClient


async def test_round_robin_advances_index(monkeypatch):
    async def fake_push(_transport, page):
        return encode_page(page)

    monkeypatch.setattr("tlsr_layout.ble.transport.BleakClient", FakeClient)
    monkeypatch.setattr("tlsr_layout.serve.push_page_via", fake_push)

    store = Store(tags={"AA": TagState()})
    rotation = ["example", "example"]
    adv = Advertisement(0xFFFFFFFF, 2950, 22, 0, False, False, False)

    assert await _maybe_push("AA", adv, rotation, store, set()) is True
    assert store.tags["AA"].page_index == 1
    assert await _maybe_push("AA", adv, rotation, store, set()) is True
    assert store.tags["AA"].page_index == 0


def test_resolve_rotation_defaults_to_registered_pages():
    names = _resolve_rotation(None)

    assert "example" in names
    assert "claude" in names
    assert "todo" in names


def test_resolve_rotation_rejects_unknown_page():
    try:
        _resolve_rotation(["missing"])
    except KeyError as exc:
        assert "unknown page" in str(exc)
    else:
        raise AssertionError("expected KeyError")
