import pytest

from tlsr_layout.ble.transport import Transport

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
