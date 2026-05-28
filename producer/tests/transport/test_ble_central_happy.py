from rcink.transport.ble.central import push_page
from rcink.transport.ble.transport import Transport
from rcink.transport.ble_encode import encode_page

from tests.transport._example_page import example_page

from .fake_peripheral import FakePeripheral


async def test_push_skips_transfer_when_data_ver_unchanged():
    """Content-addressed dedup: a matching current_data_ver short-circuits the push."""
    page = example_page()
    known = encode_page(page).data_ver
    peripheral = FakePeripheral()

    async with Transport(peripheral) as transport:
        encoded = await push_page(transport, page, current_data_ver=known)

    assert encoded.data_ver == known
    assert peripheral._begin is None  # no BEGIN frame ⇒ nothing was transferred


async def test_push_transfers_when_data_ver_differs():
    """A stale current_data_ver must NOT suppress a real change."""
    page = example_page()
    peripheral = FakePeripheral()

    async with Transport(peripheral) as transport:
        encoded = await push_page(transport, page, current_data_ver=0xDEADBEEF)

    assert peripheral.data_ver == encoded.data_ver  # full transfer happened


async def test_push_happy_path_updates_data_ver():
    peripheral = FakePeripheral()

    async with Transport(peripheral) as transport:
        encoded = await push_page(transport, example_page())

    assert peripheral.data_ver == encoded.data_ver


async def test_push_ignores_stale_progress_ack_before_commit_ack():
    peripheral = FakePeripheral(stale_progress_before_commit=True)

    async with Transport(peripheral) as transport:
        encoded = await push_page(transport, example_page())

    assert peripheral.data_ver == encoded.data_ver
