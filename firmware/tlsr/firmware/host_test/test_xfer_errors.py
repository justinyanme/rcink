import subprocess

from conftest import BIN


def test_crc_mismatch_emits_err_crc_and_ringlog():
    out = subprocess.run([BIN, "xfer-test", "crc-bad"], capture_output=True, check=True)
    assert b"OK crc-bad" in out.stdout


def test_inflate_failure_emits_err_inflate_and_ringlog():
    out = subprocess.run([BIN, "xfer-test", "inflate-bad"], capture_output=True, check=True)
    assert b"OK inflate-bad" in out.stdout


def test_parse_failure_emits_err_parse_and_ringlog():
    out = subprocess.run([BIN, "xfer-test", "parse-bad"], capture_output=True, check=True)
    assert b"OK parse-bad" in out.stdout


def test_epd_display_failure_emits_err_render_and_ringlog():
    out = subprocess.run([BIN, "xfer-test", "render-fail"], capture_output=True, check=True)
    assert b"OK render-fail" in out.stdout


def test_epd_busy_does_not_block_commit_after_display_trigger():
    out = subprocess.run([BIN, "xfer-test", "busy-display-commits"], capture_output=True, check=True)
    assert b"OK busy-display-commits" in out.stdout
