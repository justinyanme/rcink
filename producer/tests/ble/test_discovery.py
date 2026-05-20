from unittest.mock import MagicMock, patch

from tlsr_layout.ble.discovery import scan
from tlsr_layout.ble.uuids import MANUFACTURER_ID


async def test_scan_captures_one_tag():
    class FakeScanner:
        def __init__(self, detection_callback):
            self._callback = detection_callback

        async def __aenter__(self):
            device = MagicMock(address="AA:BB:CC:DD:EE:FF")
            adv = MagicMock(
                manufacturer_data={
                    MANUFACTURER_ID: (
                        bytes([0x02])
                        + b"\x01\x00\x00\x00"
                        + b"\x80\x0B"
                        + b"\x14"
                        + b"\x00"
                    )
                },
                rssi=-50,
            )
            self._callback(device, adv)
            return self

        async def __aexit__(self, *args):
            return None

    with patch("tlsr_layout.ble.discovery.BleakScanner", FakeScanner):
        result = await scan(duration_s=0.01)

    tag = result["AA:BB:CC:DD:EE:FF"]
    assert tag.address == "AA:BB:CC:DD:EE:FF"
    assert tag.rssi == -50
    assert tag.adv.data_ver == 0x00000001
    assert tag.adv.battery_mV == 0x0B80
