import json
import struct
import subprocess
from pathlib import Path

import pytest
from rcink.transport.ble.uuids import ADV_PAYLOAD_TYPE_V2
from rcink.transport.ble_protocol import (
    ACK_ERR_BUSY,
    ACK_ERR_CRC,
    ACK_ERR_FLASH,
    ACK_ERR_FORMAT,
    ACK_ERR_GAP,
    ACK_ERR_INFLATE,
    ACK_ERR_LOW_BATT,
    ACK_ERR_OVERRUN,
    ACK_ERR_PARSE,
    ACK_ERR_RENDER,
    ACK_ERR_TOO_BIG,
    ACK_OK_BEGIN,
    ACK_OK_COMMIT,
    ACK_OK_PROGRESS,
    ADV_FLAG_DISPLAY_BUSY,
    ADV_FLAG_LOW_BATT,
    ADV_FLAG_SYNC_OPEN,
    ADV_FLAG_XFER_ERR,
    DSL_FORMAT_ZLIB_JSON_V1,
    XFER_ABORT,
    XFER_BEGIN,
    XFER_BEGIN_MIN_LEN,
    XFER_BEGIN_OFF_CRC32,
    XFER_BEGIN_OFF_DATA_VER,
    XFER_BEGIN_OFF_DSL_FORMAT,
    XFER_BEGIN_OFF_RESERVED,
    XFER_BEGIN_OFF_TOTAL_CHUNKS,
    XFER_BEGIN_OFF_TOTAL_LEN,
    XFER_COMMIT,
    XFER_COMMIT_MIN_LEN,
    XFER_COMMIT_OFF_DATA_VER,
    XFER_CTL_OFF_CMD,
    XFER_DATA_OFF_PAYLOAD,
    XFER_DATA_OFF_SEQ,
    XFER_FRAME_LEN,
    XFER_MIN_BATTERY_MV,
    build_abort,
    build_begin,
    build_commit,
    build_data_chunk,
    parse_ack,
)

ROOT = Path(__file__).resolve().parents[3]
HOST_TEST = ROOT / "firmware" / "tlsr" / "firmware" / "host_test"
HOST_TEST_BIN = HOST_TEST / "build" / "host_test"


def _c_wire_constants() -> dict:
    subprocess.run(["cmake", "-S", str(HOST_TEST), "-B", str(HOST_TEST / "build")], check=True)
    subprocess.run(["cmake", "--build", str(HOST_TEST / "build"), "--target", "host_test"], check=True)
    out = subprocess.run([HOST_TEST_BIN, "wire-constants"], capture_output=True, check=True, text=True)
    return json.loads(out.stdout)


def test_python_wire_constants_match_c_host_dump():
    data = _c_wire_constants()

    assert data["ctl"] == {
        "begin": XFER_BEGIN,
        "abort": XFER_ABORT,
        "commit": XFER_COMMIT,
    }
    assert data["frame"] == {
        "len": XFER_FRAME_LEN,
        "ctl_off_cmd": XFER_CTL_OFF_CMD,
        "begin_min_len": XFER_BEGIN_MIN_LEN,
        "begin_off_data_ver": XFER_BEGIN_OFF_DATA_VER,
        "begin_off_total_len": XFER_BEGIN_OFF_TOTAL_LEN,
        "begin_off_total_chunks": XFER_BEGIN_OFF_TOTAL_CHUNKS,
        "begin_off_crc32": XFER_BEGIN_OFF_CRC32,
        "begin_off_dsl_format": XFER_BEGIN_OFF_DSL_FORMAT,
        "begin_off_reserved": XFER_BEGIN_OFF_RESERVED,
        "commit_min_len": XFER_COMMIT_MIN_LEN,
        "commit_off_data_ver": XFER_COMMIT_OFF_DATA_VER,
        "data_off_seq": XFER_DATA_OFF_SEQ,
        "data_off_payload": XFER_DATA_OFF_PAYLOAD,
        "dsl_format_zlib_json_v1": DSL_FORMAT_ZLIB_JSON_V1,
    }
    assert data["ack"] == {
        "ok_progress": ACK_OK_PROGRESS,
        "ok_begin": ACK_OK_BEGIN,
        "ok_commit": ACK_OK_COMMIT,
        "err_gap": ACK_ERR_GAP,
        "err_crc": ACK_ERR_CRC,
        "err_inflate": ACK_ERR_INFLATE,
        "err_parse": ACK_ERR_PARSE,
        "err_render": ACK_ERR_RENDER,
        "err_too_big": ACK_ERR_TOO_BIG,
        "err_format": ACK_ERR_FORMAT,
        "err_overrun": ACK_ERR_OVERRUN,
        "err_flash": ACK_ERR_FLASH,
        "err_low_batt": ACK_ERR_LOW_BATT,
        "err_busy": ACK_ERR_BUSY,
    }
    assert data["adv"] == {
        "payload_type_v2": ADV_PAYLOAD_TYPE_V2,
        "sync_open": ADV_FLAG_SYNC_OPEN,
        "low_batt": ADV_FLAG_LOW_BATT,
        "xfer_err": ADV_FLAG_XFER_ERR,
        "display_busy": ADV_FLAG_DISPLAY_BUSY,
    }
    assert data["limits"]["min_battery_mv"] == XFER_MIN_BATTERY_MV


def test_build_begin_layout():
    frame = build_begin(
        data_ver=0xDEADBEEF,
        total_len=1024,
        total_chunks=6,
        crc32=0x12345678,
        dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
    )
    assert len(frame) == XFER_FRAME_LEN
    assert frame[XFER_CTL_OFF_CMD] == XFER_BEGIN
    # Little-endian per spec
    assert struct.unpack_from("<I", frame, XFER_BEGIN_OFF_DATA_VER)[0] == 0xDEADBEEF
    assert struct.unpack_from("<H", frame, XFER_BEGIN_OFF_TOTAL_LEN)[0] == 1024
    assert struct.unpack_from("<H", frame, XFER_BEGIN_OFF_TOTAL_CHUNKS)[0] == 6
    assert struct.unpack_from("<I", frame, XFER_BEGIN_OFF_CRC32)[0] == 0x12345678
    assert frame[XFER_BEGIN_OFF_DSL_FORMAT] == DSL_FORMAT_ZLIB_JSON_V1
    assert frame[XFER_BEGIN_OFF_RESERVED:XFER_FRAME_LEN] == b"\x00" * 6


def test_build_abort():
    frame = build_abort()
    assert len(frame) == XFER_FRAME_LEN
    assert frame[XFER_CTL_OFF_CMD] == XFER_ABORT
    assert frame[1:] == b"\x00" * 19


def test_build_commit():
    frame = build_commit(data_ver=0xCAFEBABE)
    assert len(frame) == XFER_FRAME_LEN
    assert frame[XFER_CTL_OFF_CMD] == XFER_COMMIT
    assert struct.unpack_from("<I", frame, XFER_COMMIT_OFF_DATA_VER)[0] == 0xCAFEBABE
    assert frame[XFER_COMMIT_MIN_LEN:] == b"\x00" * 15


def test_build_data_chunk():
    frame = build_data_chunk(seq=7, payload=b"hello")
    assert struct.unpack_from("<H", frame, XFER_DATA_OFF_SEQ)[0] == 7
    assert frame[XFER_DATA_OFF_PAYLOAD:] == b"hello"


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
