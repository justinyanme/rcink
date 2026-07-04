import struct

from rcink.transport.ble.adv_parser import Advertisement, parse
from rcink.transport.ble.uuids import ADV_PAYLOAD_TYPE_V2, MANUFACTURER_ID
from rcink.transport.ble_protocol import (
    ADV_FLAG_DISPLAY_BUSY,
    ADV_FLAG_LOW_BATT,
    ADV_FLAG_SYNC_OPEN,
    ADV_FLAG_XFER_ERR,
)


def test_parses_v2_payload():
    flags = ADV_FLAG_SYNC_OPEN | ADV_FLAG_XFER_ERR | ADV_FLAG_DISPLAY_BUSY
    raw = bytes([ADV_PAYLOAD_TYPE_V2]) + struct.pack("<IH", 0xDEADBEEF, 2950) + bytes([-3 & 0xFF, flags])

    assert parse({MANUFACTURER_ID: raw}) == Advertisement(
        data_ver=0xDEADBEEF,
        battery_mV=2950,
        temp_c=-3,
        flags=flags,
        sync_window=True,
        low_batt=False,
        render_err=True,
        display_busy=True,
    )


def test_parses_low_battery_flag():
    flags = ADV_FLAG_SYNC_OPEN | ADV_FLAG_LOW_BATT
    raw = bytes([ADV_PAYLOAD_TYPE_V2]) + struct.pack("<IH", 0xDEADBEEF, 2300) + bytes([23, flags])

    parsed = parse({MANUFACTURER_ID: raw})
    assert parsed is not None
    assert parsed.low_batt is True
    assert parsed.render_err is False


def test_returns_none_on_wrong_manufacturer():
    assert parse({0x004C: b"\x02"}) is None


def test_returns_none_on_short_payload():
    assert parse({MANUFACTURER_ID: b"\x02\x00"}) is None


def test_returns_none_on_unknown_type():
    raw = bytes([0xFF]) + b"\x00" * 8

    assert parse({MANUFACTURER_ID: raw}) is None
