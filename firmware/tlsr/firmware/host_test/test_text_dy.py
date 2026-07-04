import subprocess

from conftest import BIN


def test_text_dy_round_trip():
    out = subprocess.run([BIN, "text-dy-test"], capture_output=True, check=True)
    assert b"OK text-dy" in out.stdout
