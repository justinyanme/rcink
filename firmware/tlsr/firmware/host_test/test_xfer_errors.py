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


def test_begin_too_big_emits_err_too_big_and_ringlog():
    out = subprocess.run([BIN, "xfer-test", "too-big"], capture_output=True, check=True)
    assert b"OK too-big" in out.stdout


def test_begin_bad_format_emits_err_format_and_ringlog():
    out = subprocess.run([BIN, "xfer-test", "format-bad"], capture_output=True, check=True)
    assert b"OK format-bad" in out.stdout


def test_data_overrun_emits_err_overrun_and_ringlog():
    out = subprocess.run([BIN, "xfer-test", "overrun"], capture_output=True, check=True)
    assert b"OK overrun" in out.stdout


def test_flash_write_failure_emits_err_flash_and_ringlog():
    out = subprocess.run([BIN, "xfer-test", "flash-fail"], capture_output=True, check=True)
    assert b"OK flash-fail" in out.stdout


def test_invalid_commit_validation_returns_to_idle():
    out = subprocess.run([BIN, "xfer-test", "commit-invalid-idles"], capture_output=True, check=True)
    assert b"OK commit-invalid-idles" in out.stdout
