import struct

import pytest

from tlsr_layout.protocol import (
    DSL_FORMAT_ZLIB_JSON_V1,
    XFER_ABORT,
    XFER_BEGIN,
    XFER_COMMIT,
    build_abort,
    build_begin,
    build_commit,
    build_data_chunk,
    parse_ack,
)


def test_build_begin_layout():
    frame = build_begin(
        data_ver=0xDEADBEEF,
        total_len=1024,
        total_chunks=6,
        crc32=0x12345678,
        dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
    )
    assert len(frame) == 20
    assert frame[0] == XFER_BEGIN
    # Little-endian per spec
    assert struct.unpack_from("<I", frame, 1)[0] == 0xDEADBEEF
    assert struct.unpack_from("<H", frame, 5)[0] == 1024
    assert struct.unpack_from("<H", frame, 7)[0] == 6
    assert struct.unpack_from("<I", frame, 9)[0] == 0x12345678
    assert frame[13] == DSL_FORMAT_ZLIB_JSON_V1
    assert frame[14:20] == b"\x00" * 6


def test_build_abort():
    frame = build_abort()
    assert len(frame) == 20
    assert frame[0] == XFER_ABORT
    assert frame[1:] == b"\x00" * 19


def test_build_commit():
    frame = build_commit(data_ver=0xCAFEBABE)
    assert len(frame) == 20
    assert frame[0] == XFER_COMMIT
    assert struct.unpack_from("<I", frame, 1)[0] == 0xCAFEBABE
    assert frame[5:] == b"\x00" * 15


def test_build_data_chunk():
    frame = build_data_chunk(seq=7, payload=b"hello")
    assert frame == struct.pack("<H", 7) + b"hello"


def test_build_data_chunk_rejects_oversize_seq():
    with pytest.raises(ValueError):
        build_data_chunk(seq=-1, payload=b"x")
    with pytest.raises(ValueError):
        build_data_chunk(seq=0x10000, payload=b"x")


def test_build_begin_rejects_out_of_range():
    with pytest.raises(ValueError, match="total_len"):
        build_begin(
            data_ver=0,
            total_len=0x10000,  # >u16
            total_chunks=1,
            crc32=0,
            dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
        )
    with pytest.raises(ValueError, match="total_chunks"):
        build_begin(
            data_ver=0,
            total_len=1,
            total_chunks=0x10000,
            crc32=0,
            dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
        )


def test_parse_ack_progress():
    raw = struct.pack("<HBB", 42, 0x00, 0x00)
    ack = parse_ack(raw)
    assert ack.last_seq == 42
    assert ack.code == 0x00
    assert ack.flags == 0
    assert ack.is_ok is True
    assert ack.is_error is False


def test_parse_ack_error():
    raw = struct.pack("<HBB", 5, 0x10, 0x00)  # ERR_GAP
    ack = parse_ack(raw)
    assert ack.code == 0x10
    assert ack.is_ok is False
    assert ack.is_error is True


def test_parse_ack_rejects_wrong_length():
    with pytest.raises(ValueError, match="ack must be 4 bytes"):
        parse_ack(b"\x00" * 3)
    with pytest.raises(ValueError, match="ack must be 4 bytes"):
        parse_ack(b"\x00" * 5)
