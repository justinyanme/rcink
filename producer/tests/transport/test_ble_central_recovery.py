import pytest

from rcink.transport.ble.central import PushError, push_page
from rcink.transport.ble.transport import Transport
from tests.transport._example_page import example_page
from rcink.transport.ble_protocol import ACK_ERR_CRC, ACK_ERR_LOW_BATT

from .fake_peripheral import FakePeripheral


async def test_push_handles_gap_and_recovers():
    peripheral = FakePeripheral(drop_chunks=[2])

    async with Transport(peripheral) as transport:
        encoded = await push_page(transport, example_page(), chunk_payload_size=8)

    assert peripheral.data_ver == encoded.data_ver


async def test_push_restarts_once_on_crc_and_recovers():
    peripheral = FakePeripheral(crc_errors_remaining=1)

    async with Transport(peripheral) as transport:
        encoded = await push_page(transport, example_page())

    assert peripheral.data_ver == encoded.data_ver


async def test_push_raises_on_low_batt():
    peripheral = FakePeripheral(forced_low_batt=True)

    async with Transport(peripheral) as transport:
        with pytest.raises(PushError) as exc_info:
            await push_page(transport, example_page())

    assert exc_info.value.code == ACK_ERR_LOW_BATT


async def test_push_raises_on_persistent_crc():
    peripheral = FakePeripheral(forced_crc_error=True)

    async with Transport(peripheral) as transport:
        with pytest.raises(PushError) as exc_info:
            await push_page(transport, example_page())

    assert exc_info.value.code == ACK_ERR_CRC
