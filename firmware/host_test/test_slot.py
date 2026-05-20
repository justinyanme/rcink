import subprocess

from conftest import ROOT

BIN = ROOT / "build" / "host_test"


def test_slot_roundtrip():
    out = subprocess.run([BIN, "slot-test", "roundtrip"], capture_output=True, check=True)
    assert b"OK" in out.stdout


def test_slot_picks_highest_seq():
    out = subprocess.run([BIN, "slot-test", "highest-seq"], capture_output=True, check=True)
    assert b"OK" in out.stdout


def test_slot_falls_back_on_invalid_crc():
    out = subprocess.run([BIN, "slot-test", "invalid-crc"], capture_output=True, check=True)
    assert b"OK" in out.stdout
