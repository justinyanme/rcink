"""Wire-format frame builders for docs/tlsr-ble-protocol.md.

All multi-byte fields are little-endian. Reserved fields are zero-filled.
"""

import struct
from dataclasses import dataclass

XFER_BEGIN = 0x01
XFER_ABORT = 0x02
XFER_COMMIT = 0x03

XFER_FRAME_LEN = 20

XFER_CTL_OFF_CMD = 0
XFER_BEGIN_MIN_LEN = 14
XFER_BEGIN_OFF_DATA_VER = 1
XFER_BEGIN_OFF_TOTAL_LEN = 5
XFER_BEGIN_OFF_TOTAL_CHUNKS = 7
XFER_BEGIN_OFF_CRC32 = 9
XFER_BEGIN_OFF_DSL_FORMAT = 13
XFER_BEGIN_OFF_RESERVED = 14

XFER_COMMIT_MIN_LEN = 5
XFER_COMMIT_OFF_DATA_VER = 1

XFER_DATA_OFF_SEQ = 0
XFER_DATA_OFF_PAYLOAD = 2

DSL_FORMAT_ZLIB_JSON_V1 = 0x01
XFER_MIN_BATTERY_MV = 2400

ADV_FLAG_SYNC_OPEN = 0x01
ADV_FLAG_LOW_BATT = 0x02
ADV_FLAG_XFER_ERR = 0x04
ADV_FLAG_DISPLAY_BUSY = 0x08

ACK_OK_PROGRESS = 0x00
ACK_OK_BEGIN = 0x01
ACK_OK_COMMIT = 0x02
ACK_ERR_GAP = 0x10
ACK_ERR_CRC = 0x11
ACK_ERR_INFLATE = 0x12
ACK_ERR_PARSE = 0x13
ACK_ERR_RENDER = 0x14
ACK_ERR_TOO_BIG = 0x15
ACK_ERR_FORMAT = 0x16
ACK_ERR_OVERRUN = 0x17
ACK_ERR_FLASH = 0x18
ACK_ERR_LOW_BATT = 0x20
ACK_ERR_BUSY = 0x21


def _check_u16(name: str, value: int) -> None:
    if not 0 <= value <= 0xFFFF:
        raise ValueError(f"{name} out of u16 range: {value}")


def _check_u32(name: str, value: int) -> None:
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError(f"{name} out of u32 range: {value}")


def build_begin(
    *,
    data_ver: int,
    total_len: int,
    total_chunks: int,
    crc32: int,
    dsl_format: int,
) -> bytes:
    _check_u32("data_ver", data_ver)
    _check_u16("total_len", total_len)
    _check_u16("total_chunks", total_chunks)
    _check_u32("crc32", crc32)
    if not 0 <= dsl_format <= 0xFF:
        raise ValueError(f"dsl_format out of u8 range: {dsl_format}")
    frame = bytearray(XFER_FRAME_LEN)
    frame[XFER_CTL_OFF_CMD] = XFER_BEGIN
    struct.pack_into("<I", frame, XFER_BEGIN_OFF_DATA_VER, data_ver)
    struct.pack_into("<H", frame, XFER_BEGIN_OFF_TOTAL_LEN, total_len)
    struct.pack_into("<H", frame, XFER_BEGIN_OFF_TOTAL_CHUNKS, total_chunks)
    struct.pack_into("<I", frame, XFER_BEGIN_OFF_CRC32, crc32)
    frame[XFER_BEGIN_OFF_DSL_FORMAT] = dsl_format
    return bytes(frame)


def build_abort() -> bytes:
    frame = bytearray(XFER_FRAME_LEN)
    frame[XFER_CTL_OFF_CMD] = XFER_ABORT
    return bytes(frame)


def build_commit(*, data_ver: int) -> bytes:
    _check_u32("data_ver", data_ver)
    frame = bytearray(XFER_FRAME_LEN)
    frame[XFER_CTL_OFF_CMD] = XFER_COMMIT
    struct.pack_into("<I", frame, XFER_COMMIT_OFF_DATA_VER, data_ver)
    return bytes(frame)


def build_data_chunk(*, seq: int, payload: bytes) -> bytes:
    _check_u16("seq", seq)
    frame = bytearray(XFER_DATA_OFF_PAYLOAD + len(payload))
    struct.pack_into("<H", frame, XFER_DATA_OFF_SEQ, seq)
    frame[XFER_DATA_OFF_PAYLOAD:] = payload
    return bytes(frame)


@dataclass(slots=True, frozen=True)
class Ack:
    last_seq: int
    code: int
    flags: int

    @property
    def is_ok(self) -> bool:
        return self.code < 0x10

    @property
    def is_error(self) -> bool:
        return self.code >= 0x10


def parse_ack(raw: bytes) -> Ack:
    if len(raw) != 4:
        raise ValueError(f"ack must be 4 bytes, got {len(raw)}")
    last_seq, code, flags = struct.unpack("<HBB", raw)
    return Ack(last_seq=last_seq, code=code, flags=flags)
