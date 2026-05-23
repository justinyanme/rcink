"""Wire-format frame builders for spec §6.

All multi-byte fields are little-endian. Reserved fields are zero-filled.
"""

import struct
from dataclasses import dataclass

XFER_BEGIN = 0x01
XFER_ABORT = 0x02
XFER_COMMIT = 0x03

DSL_FORMAT_ZLIB_JSON_V1 = 0x01

ACK_OK_PROGRESS = 0x00
ACK_OK_BEGIN = 0x01
ACK_OK_COMMIT = 0x02
ACK_ERR_GAP = 0x10
ACK_ERR_CRC = 0x11
ACK_ERR_INFLATE = 0x12
ACK_ERR_PARSE = 0x13
ACK_ERR_RENDER = 0x14
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
    return (
        bytes([XFER_BEGIN])
        + struct.pack("<IHHIB", data_ver, total_len, total_chunks, crc32, dsl_format)
        + b"\x00" * 6
    )


def build_abort() -> bytes:
    return bytes([XFER_ABORT]) + b"\x00" * 19


def build_commit(*, data_ver: int) -> bytes:
    _check_u32("data_ver", data_ver)
    return bytes([XFER_COMMIT]) + struct.pack("<I", data_ver) + b"\x00" * 15


def build_data_chunk(*, seq: int, payload: bytes) -> bytes:
    _check_u16("seq", seq)
    return struct.pack("<H", seq) + payload


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
