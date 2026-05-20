from tlsr_layout.ble.central import push_page
from tlsr_layout.ble.transport import Transport
from tlsr_layout.pages.example import example_page

from .fake_peripheral import FakePeripheral


async def test_push_happy_path_updates_data_ver():
    peripheral = FakePeripheral()

    async with Transport(peripheral) as transport:
        encoded = await push_page(transport, example_page())

    assert peripheral.data_ver == encoded.data_ver
