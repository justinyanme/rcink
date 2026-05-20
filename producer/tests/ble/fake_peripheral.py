"""In-memory peripheral implementing the tlsr-layout transfer protocol."""

import struct
import zlib
from collections.abc import Callable
from dataclasses import dataclass, field

from tlsr_layout.ble.uuids import (
    CHAR_META,
    CHAR_STATUS,
    CHAR_XFER_ACK,
    CHAR_XFER_CTL,
    CHAR_XFER_DATA,
)
from tlsr_layout.protocol import (
    ACK_ERR_CRC,
    ACK_ERR_GAP,
    ACK_ERR_INFLATE,
    ACK_ERR_LOW_BATT,
    ACK_OK_BEGIN,
    ACK_OK_COMMIT,
    ACK_OK_PROGRESS,
    XFER_ABORT,
    XFER_BEGIN,
    XFER_COMMIT,
)


@dataclass(slots=True)
class FakePeripheral:
    address: str = "AA:BB:CC:DD:EE:FF"
    data_ver: int = 0
    battery_mV: int = 2950
    forced_low_batt: bool = False
    forced_crc_error: bool = False
    forced_inflate_error: bool = False
    drop_chunks: list[int] = field(default_factory=list)

    _staging: bytearray = field(default_factory=bytearray)
    _begin: dict[str, int] | None = None
    _ack_cb: Callable[[int, bytearray], None] | None = None
    _expected_seq: int = 0

    async def __aenter__(self) -> "FakePeripheral":
        return self

    async def __aexit__(self, *args) -> None:
        return None

    async def start_notify(self, uuid: str, callback: Callable[[int, bytearray], None]):
        if uuid == CHAR_XFER_ACK:
            self._ack_cb = callback

    async def stop_notify(self, uuid: str):
        if uuid == CHAR_XFER_ACK:
            self._ack_cb = None

    async def read_gatt_char(self, uuid: str) -> bytes:
        if uuid == CHAR_STATUS:
            return struct.pack(
                "<IBBHbB6x",
                self.data_ver,
                0,
                0,
                self.battery_mV,
                23,
                0,
            )
        if uuid == CHAR_META:
            return (
                struct.pack("<HIHHBHIH", 1, 0x040000, 250, 128, 2, 0b111, 131072, 185)
                + b"\x00" * 13
            )
        return b""

    async def write_gatt_char(
        self,
        uuid: str,
        frame: bytes | bytearray,
        response: bool = True,
    ) -> None:
        del response
        if uuid == CHAR_XFER_CTL:
            await self._handle_ctl(bytes(frame))
            return
        if uuid == CHAR_XFER_DATA:
            await self._handle_data(bytes(frame))
            return

    def _ack(self, last_seq: int, code: int, flags: int = 0) -> None:
        if self._ack_cb is not None:
            self._ack_cb(0, bytearray(struct.pack("<HBB", last_seq, code, flags)))

    async def _handle_ctl(self, frame: bytes) -> None:
        cmd = frame[0]
        if cmd == XFER_BEGIN:
            if self.forced_low_batt or self.battery_mV < 2400:
                self._ack(0, ACK_ERR_LOW_BATT)
                return
            data_ver, total_len, total_chunks, crc32, dsl_format = struct.unpack_from(
                "<IHHIB", frame, 1
            )
            self._begin = {
                "data_ver": data_ver,
                "total_len": total_len,
                "total_chunks": total_chunks,
                "crc32": crc32,
                "dsl_format": dsl_format,
            }
            self._staging = bytearray()
            self._expected_seq = 0
            self._ack(0, ACK_OK_BEGIN)
            return
        if cmd == XFER_ABORT:
            self._begin = None
            self._staging = bytearray()
            self._expected_seq = 0
            return
        if cmd == XFER_COMMIT:
            if self._begin is None:
                self._ack(0, ACK_ERR_GAP)
                return
            if len(self._staging) != self._begin["total_len"]:
                self._ack(max(0, self._expected_seq - 1), ACK_ERR_GAP)
                return
            if self.forced_crc_error or (
                zlib.crc32(self._staging) & 0xFFFFFFFF
            ) != self._begin["crc32"]:
                self._ack(0, ACK_ERR_CRC)
                return
            if self.forced_inflate_error:
                self._ack(0, ACK_ERR_INFLATE)
                return
            try:
                zlib.decompress(bytes(self._staging))
            except zlib.error:
                self._ack(0, ACK_ERR_INFLATE)
                return
            self.data_ver = self._begin["data_ver"]
            self._ack(0, ACK_OK_COMMIT)

    async def _handle_data(self, frame: bytes) -> None:
        seq = struct.unpack_from("<H", frame)[0]
        payload = frame[2:]
        if seq in self.drop_chunks:
            self.drop_chunks.remove(seq)
            return
        if seq != self._expected_seq:
            self._ack(max(0, self._expected_seq - 1), ACK_ERR_GAP)
            return
        self._staging.extend(payload)
        self._expected_seq += 1
        if self._expected_seq % 8 == 0:
            self._ack(seq, ACK_OK_PROGRESS)
