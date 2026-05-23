import subprocess

from conftest import BIN


def test_watchdog_aborts_stuck_transfer_without_ack():
    out = subprocess.run([BIN, "xfer-test", "watchdog"], capture_output=True, check=True)
    assert b"OK watchdog" in out.stdout
