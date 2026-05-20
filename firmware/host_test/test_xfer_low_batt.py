import subprocess

from conftest import BIN


def test_low_battery_refused_at_begin():
    out = subprocess.run([BIN, "xfer-test", "low-batt"], capture_output=True, check=True)
    assert b"OK low-batt" in out.stdout
