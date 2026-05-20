import subprocess

from conftest import BIN


def test_ringlog_basic_append():
    out = subprocess.run([BIN, "ringlog-test", "basic-append"], capture_output=True, check=True)
    assert b"OK" in out.stdout


def test_ringlog_wraps_after_eight():
    out = subprocess.run([BIN, "ringlog-test", "wraps-after-eight"], capture_output=True, check=True)
    assert b"OK" in out.stdout


def test_ringlog_init_finds_existing_tail():
    out = subprocess.run([BIN, "ringlog-test", "init-finds-existing-tail"], capture_output=True, check=True)
    assert b"OK" in out.stdout
