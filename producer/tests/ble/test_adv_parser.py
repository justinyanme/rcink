import struct

from tlsr_layout.ble.adv_parser import Advertisement, parse
from tlsr_layout.ble.uuids import MANUFACTURER_ID


def test_parses_v2_payload():
    raw = (
        bytes([0x02])
        + struct.pack("<IH", 0xDEADBEEF, 2950)
        + bytes([-3 & 0xFF, 0x05])
    )

    assert parse({MANUFACTURER_ID: raw}) == Advertisement(
        data_ver=0xDEADBEEF,
        battery_mV=2950,
        temp_c=-3,
        flags=0x05,
        sync_window=True,
        low_batt=False,
        render_err=True,
    )


def test_returns_none_on_wrong_manufacturer():
    assert parse({0x004C: b"\x02"}) is None


def test_returns_none_on_short_payload():
    assert parse({MANUFACTURER_ID: b"\x02\x00"}) is None


def test_returns_none_on_unknown_type():
    raw = bytes([0xFF]) + b"\x00" * 8

    assert parse({MANUFACTURER_ID: raw}) is None
