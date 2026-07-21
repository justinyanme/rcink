import pytest

from rcink.transport.ble.central import PushError, push_page
from rcink.transport.ble.transport import Transport
from rcink.transport.ble_protocol import (
    ACK_ERR_BUSY,
    ACK_ERR_CRC,
    ACK_ERR_FORMAT,
    ACK_ERR_INFLATE,
    ACK_ERR_LOW_BATT,
    ACK_ERR_RENDER,
)
from tests.transport._example_page import example_page

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
    assert peripheral.abort_count == 1


async def test_push_raises_clear_error_when_begin_ack_missing():
    peripheral = FakePeripheral(drop_begin_ack=True)

    async with Transport(peripheral) as transport:
        with pytest.raises(PushError, match="BEGIN ack timeout") as exc_info:
            await push_page(transport, example_page(), ack_timeout_s=0.01)

    assert exc_info.value.code == -1


async def test_push_raises_on_persistent_crc():
    peripheral = FakePeripheral(forced_crc_error=True)

    async with Transport(peripheral) as transport:
        with pytest.raises(PushError) as exc_info:
            await push_page(transport, example_page())

    assert exc_info.value.code == ACK_ERR_CRC
    assert peripheral.abort_count == 2


async def test_push_aborts_on_begin_rejection():
    peripheral = FakePeripheral(forced_begin_code=ACK_ERR_FORMAT)

    async with Transport(peripheral) as transport:
        with pytest.raises(PushError, match="wire format rejected") as exc_info:
            await push_page(transport, example_page())

    assert exc_info.value.code == ACK_ERR_FORMAT
    assert peripheral.abort_count == 1


async def test_push_aborts_on_commit_rejection():
    peripheral = FakePeripheral(forced_inflate_error=True)

    async with Transport(peripheral) as transport:
        with pytest.raises(PushError) as exc_info:
            await push_page(transport, example_page())

    assert exc_info.value.code == ACK_ERR_INFLATE
    assert peripheral.abort_count == 1


async def test_push_maps_render_commit_rejection():
    peripheral = FakePeripheral(forced_commit_code=ACK_ERR_RENDER)

    async with Transport(peripheral) as transport:
        with pytest.raises(PushError, match="render failed") as exc_info:
            await push_page(transport, example_page())

    assert exc_info.value.code == ACK_ERR_RENDER
    assert peripheral.abort_count == 1


async def test_push_maps_busy_commit_rejection():
    peripheral = FakePeripheral(forced_commit_code=ACK_ERR_BUSY)

    async with Transport(peripheral) as transport:
        with pytest.raises(PushError, match="tag busy") as exc_info:
            await push_page(transport, example_page())

    assert exc_info.value.code == ACK_ERR_BUSY
    assert peripheral.abort_count == 1
