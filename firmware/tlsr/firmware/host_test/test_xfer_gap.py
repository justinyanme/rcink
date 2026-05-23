import subprocess

from conftest import BIN


def test_gap_emits_err_gap_with_last_contiguous_seq():
    out = subprocess.run([BIN, "xfer-test", "gap"], capture_output=True, check=True)
    assert b"OK gap" in out.stdout
