import subprocess

from conftest import BIN


def test_low_battery_refused_at_begin():
    out = subprocess.run([BIN, "xfer-test", "low-batt"], capture_output=True, check=True)
    assert b"OK low-batt" in out.stdout


def test_cached_good_battery_allows_begin_after_bad_adc_sample():
    out = subprocess.run([BIN, "xfer-test", "cached-batt"], capture_output=True, check=True)
    assert b"OK cached-batt" in out.stdout
