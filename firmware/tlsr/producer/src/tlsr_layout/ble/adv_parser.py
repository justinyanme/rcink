"""Decode manufacturer-data payloads from tlsr-layout advertisements."""

import struct
from dataclasses import dataclass

from .uuids import ADV_PAYLOAD_TYPE_V2, MANUFACTURER_ID


@dataclass(slots=True, frozen=True)
class Advertisement:
    data_ver: int
    battery_mV: int
    temp_c: int
    flags: int
    sync_window: bool
    low_batt: bool
    render_err: bool
    display_busy: bool = False


def parse(manufacturer_data: dict[int, bytes]) -> Advertisement | None:
    """Parse spec §5.2 payload v2, returning None for non tlsr-layout ads."""

    raw = manufacturer_data.get(MANUFACTURER_ID)
    if raw is None or len(raw) < 9:
        return None
    if raw[0] != ADV_PAYLOAD_TYPE_V2:
        return None

    data_ver, battery_mV = struct.unpack_from("<IH", raw, 1)
    temp_c, flags = struct.unpack_from("<bB", raw, 7)
    return Advertisement(
        data_ver=data_ver,
        battery_mV=battery_mV,
        temp_c=temp_c,
        flags=flags,
        sync_window=bool(flags & 0x01),
        low_batt=bool(flags & 0x02),
        render_err=bool(flags & 0x04),
        display_busy=bool(flags & 0x08),
    )
