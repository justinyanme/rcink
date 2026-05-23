"""Connection-scoped GATT operations for tlsr-layout tags."""

import asyncio
import struct
import sys
from contextlib import asynccontextmanager
from dataclasses import dataclass
from typing import Any

from bleak import BleakClient
from bleak.exc import BleakDeviceNotFoundError

from .uuids import (
    CHAR_META,
    CHAR_STATUS,
    CHAR_XFER_ACK,
    CHAR_XFER_CTL,
    CHAR_XFER_DATA,
)

DEFAULT_CONNECT_TIMEOUT_S = 30.0


@dataclass(slots=True, frozen=True)
class Meta:
    hw_id: int
    fw_ver: int
    epd_w: int
    epd_h: int
    epd_bpp: int
    palette_mask: int
    flash_capacity_b: int
    mtu_pref: int


class Transport:
    """Thin wrapper around BleakClient plus an ACK notification queue."""

    def __init__(self, client: BleakClient) -> None:
        self._client = client
        self._ack_queue: asyncio.Queue[bytes] = asyncio.Queue()

    async def __aenter__(self) -> "Transport":
        await self._client.__aenter__()
        await self._client.start_notify(CHAR_XFER_ACK, self._on_ack)
        return self

    async def __aexit__(self, *args: Any) -> None:
        try:
            await self._client.stop_notify(CHAR_XFER_ACK)
        finally:
            await self._client.__aexit__(*args)

    def _on_ack(self, _handle: int, data: bytearray) -> None:
        self._ack_queue.put_nowait(bytes(data))

    async def read_meta(self) -> Meta:
        raw = bytes(await self._client.read_gatt_char(CHAR_META))
        if len(raw) < 19:
            raise ValueError(f"META must be at least 19 bytes, got {len(raw)}")
        hw_id, fw_ver, epd_w, epd_h, epd_bpp, palette_mask, flash_capacity_b, mtu_pref = (
            struct.unpack_from("<HIHHBHIH", raw)
        )
        return Meta(
            hw_id=hw_id,
            fw_ver=fw_ver,
            epd_w=epd_w,
            epd_h=epd_h,
            epd_bpp=epd_bpp,
            palette_mask=palette_mask,
            flash_capacity_b=flash_capacity_b,
            mtu_pref=mtu_pref,
        )

    async def read_status(self) -> bytes:
        return bytes(await self._client.read_gatt_char(CHAR_STATUS))

    async def write_ctl(self, frame: bytes) -> None:
        await self._client.write_gatt_char(CHAR_XFER_CTL, frame, response=False)

    async def write_data(self, frame: bytes) -> None:
        await self._client.write_gatt_char(CHAR_XFER_DATA, frame, response=False)

    async def next_ack(self, timeout_s: float) -> bytes:
        return await asyncio.wait_for(self._ack_queue.get(), timeout=timeout_s)


@asynccontextmanager
async def connect_transport(
    address: str,
    *,
    device: Any | None = None,
    timeout_s: float = DEFAULT_CONNECT_TIMEOUT_S,
):
    """Open a Transport using the scanned device first, with address lookup as fallback."""

    primary = device if device is not None else address
    fallback = address if device is not None else None

    transport = Transport(BleakClient(primary, timeout=timeout_s))
    try:
        await transport.__aenter__()
    except (BleakDeviceNotFoundError, TimeoutError):
        if fallback is None:
            raise
    else:
        exc_info = (None, None, None)
        try:
            yield transport
        except BaseException:
            exc_info = sys.exc_info()
            raise
        finally:
            await transport.__aexit__(*exc_info)
        return

    async with Transport(BleakClient(fallback, timeout=timeout_s)) as transport:
        yield transport
