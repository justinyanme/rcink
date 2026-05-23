from unittest.mock import patch

import pytest

from rcink.transport.ble.transport import Transport, connect_transport

from .fake_peripheral import FakePeripheral


async def test_transport_meta_and_status():
    async with Transport(FakePeripheral()) as transport:
        meta = await transport.read_meta()
        status = await transport.read_status()

    assert meta.epd_w == 250
    assert meta.epd_h == 128
    assert meta.mtu_pref == 185
    assert len(status) == 16


async def test_transport_rejects_short_meta():
    class ShortMetaPeripheral(FakePeripheral):
        async def read_gatt_char(self, uuid: str) -> bytes:
            return b"\x00" * 18

    async with Transport(ShortMetaPeripheral()) as transport:
        with pytest.raises(ValueError, match="META"):
            await transport.read_meta()


async def test_connect_transport_uses_scanned_device_before_address_lookup():
    peripheral = FakePeripheral()
    calls = []

    def fake_client(target, *, timeout):
        calls.append((target, timeout))
        return target

    with patch("rcink.transport.ble.transport.BleakClient", side_effect=fake_client):
        async with connect_transport("AA:BB:CC:DD:EE:FF", device=peripheral) as transport:
            meta = await transport.read_meta()

    assert meta.epd_w == 250
    assert calls == [(peripheral, 30.0)]


async def test_connect_transport_falls_back_to_address_when_scanned_device_times_out():
    peripheral = FakePeripheral()
    address_peripheral = FakePeripheral()
    calls = []

    class TimeoutClient:
        def __init__(self, target):
            self.target = target

        async def __aenter__(self):
            raise TimeoutError

        async def __aexit__(self, *_args):
            return None

    def fake_client(target, *, timeout):
        calls.append((target, timeout))
        if target is peripheral:
            return TimeoutClient(target)
        return address_peripheral

    with patch("rcink.transport.ble.transport.BleakClient", side_effect=fake_client):
        async with connect_transport("AA:BB:CC:DD:EE:FF", device=peripheral) as transport:
            meta = await transport.read_meta()

    assert meta.epd_w == 250
    assert transport._client is address_peripheral
    assert calls == [(peripheral, 30.0), ("AA:BB:CC:DD:EE:FF", 30.0)]
