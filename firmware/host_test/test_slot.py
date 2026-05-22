import re
import subprocess

from conftest import ROOT

BIN = ROOT / "build" / "host_test"
SRC = ROOT.parent / "src"


def test_slot_roundtrip():
    out = subprocess.run([BIN, "slot-test", "roundtrip"], capture_output=True, check=True)
    assert b"OK" in out.stdout


def test_slot_picks_highest_seq():
    out = subprocess.run([BIN, "slot-test", "highest-seq"], capture_output=True, check=True)
    assert b"OK" in out.stdout


def test_slot_falls_back_on_invalid_crc():
    out = subprocess.run([BIN, "slot-test", "invalid-crc"], capture_output=True, check=True)
    assert b"OK" in out.stdout


def test_layout_flash_sectors_do_not_overlap_telink_sdk_reserved_sectors():
    reserved = {
        0x73000,  # tlsr-price-tag quote sector kept clear for dual-flash recovery
        0x74000,  # Telink SMP pairing on 512 KB flash
        0x76000,  # Telink public MAC on 512 KB flash
        0x77000,  # Telink calibration on 512 KB flash
    }
    text = (SRC / "store" / "slot.h").read_text() + (SRC / "store" / "ringlog.h").read_text()
    values = {
        name: int(value, 16)
        for name, value in re.findall(r"#define\s+(SLOT_[AB]_ADDR|RINGLOG_ADDR)\s+0x([0-9A-Fa-f]+)u", text)
    }

    assert values == {
        "SLOT_A_ADDR": 0x70000,
        "SLOT_B_ADDR": 0x71000,
        "RINGLOG_ADDR": 0x72000,
    }
    assert reserved.isdisjoint(values.values())
