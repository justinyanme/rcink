from unittest.mock import AsyncMock, patch

from tlsr_layout.ble.adv_parser import Advertisement
from tlsr_layout.ble.central import PushError
from tlsr_layout.encode import encode_page
from tlsr_layout.pages.registry import build_page
from tlsr_layout.serve import _maybe_push
from tlsr_layout.state import Store, TagState


class FakeClient:
    def __init__(self, *_args, **_kwargs) -> None:
        pass

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_args):
        return None

    async def start_notify(self, *_args, **_kwargs) -> None:
        pass

    async def stop_notify(self, *_args, **_kwargs) -> None:
        pass


def _adv(data_ver: int, *, low_batt: bool = False) -> Advertisement:
    return Advertisement(
        data_ver=data_ver,
        battery_mV=2300 if low_batt else 2950,
        temp_c=22,
        flags=0x02 if low_batt else 0,
        sync_window=False,
        low_batt=low_batt,
        render_err=False,
    )


async def test_skips_when_in_sync():
    page = build_page("example")
    store = Store(tags={"AA:BB:CC:DD:EE:FF": TagState(page_index=0)})
    busy: set[str] = set()

    with patch("tlsr_layout.serve.push_page_via", new=AsyncMock()) as push:
        changed = await _maybe_push(
            "AA:BB:CC:DD:EE:FF",
            _adv(encode_page(page).data_ver),
            ["example"],
            store,
            busy,
        )

    assert changed is False
    assert busy == set()
    push.assert_not_called()


async def test_skips_on_low_battery():
    store = Store(tags={"AA:BB:CC:DD:EE:FF": TagState()})

    with patch("tlsr_layout.serve.push_page_via", new=AsyncMock()) as push:
        changed = await _maybe_push(
            "AA:BB:CC:DD:EE:FF",
            _adv(0, low_batt=True),
            ["example"],
            store,
            set(),
        )

    assert changed is False
    push.assert_not_called()


async def test_skips_when_already_busy():
    store = Store(tags={"AA:BB:CC:DD:EE:FF": TagState()})
    busy = {"AA:BB:CC:DD:EE:FF"}

    with patch("tlsr_layout.serve.push_page_via", new=AsyncMock()) as push:
        changed = await _maybe_push(
            "AA:BB:CC:DD:EE:FF",
            _adv(0),
            ["example"],
            store,
            busy,
        )

    assert changed is False
    assert busy == {"AA:BB:CC:DD:EE:FF"}
    push.assert_not_called()


async def test_pushes_when_stale(monkeypatch):
    page = build_page("example")
    store = Store(tags={"AA:BB:CC:DD:EE:FF": TagState()})
    pushed: list[dict] = []

    async def fake_push(_transport, pushed_page):
        pushed.append(pushed_page)
        return encode_page(pushed_page)

    monkeypatch.setattr("tlsr_layout.ble.transport.BleakClient", FakeClient)
    monkeypatch.setattr("tlsr_layout.serve.push_page_via", fake_push)

    changed = await _maybe_push(
        "AA:BB:CC:DD:EE:FF",
        _adv(0),
        ["example"],
        store,
        set(),
    )

    assert changed is True
    assert pushed == [page]
    tag = store.tags["AA:BB:CC:DD:EE:FF"]
    assert tag.last_data_ver == encode_page(page).data_ver
    assert tag.last_pushed_at > 0
    assert tag.page_index == 0


async def test_push_failure_does_not_advance(monkeypatch):
    store = Store(tags={"AA:BB:CC:DD:EE:FF": TagState()})

    async def fail_push(_transport, _page):
        raise PushError(0x11, "CRC mismatch")

    monkeypatch.setattr("tlsr_layout.ble.transport.BleakClient", FakeClient)
    monkeypatch.setattr("tlsr_layout.serve.push_page_via", fail_push)

    changed = await _maybe_push(
        "AA:BB:CC:DD:EE:FF",
        _adv(0),
        ["example"],
        store,
        set(),
    )

    assert changed is False
    assert store.tags["AA:BB:CC:DD:EE:FF"].page_index == 0
    assert store.tags["AA:BB:CC:DD:EE:FF"].last_data_ver == 0
