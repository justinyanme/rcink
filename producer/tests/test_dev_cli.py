"""Console safety boundaries and realistic interrupted-session evidence."""

import json
from collections import deque

import pytest
import serial
from scripts import dev_cli
from typer.testing import CliRunner

runner = CliRunner()


class Console:
    def __init__(self, *, chunks=(), short_write=False, after_read=None):
        self.chunks = deque(chunks)
        self.short_write = short_write
        self.after_read = after_read
        self.opens = 0
        self.closed = False
        self.written = bytearray()
        self.dtr = True
        self.rts = True

    def open(self):
        # pyserial contract: deferred open, inactive control lines, exclusive lock.
        if self.dtr or self.rts:
            raise AssertionError("Open would assert a reset control line")
        self.opens += 1
        if self.opens != 1:
            raise AssertionError("Disconnected sessions must never reopen")

    def close(self):
        self.closed = True

    def write(self, data):
        if self.short_write:
            return 0
        self.written.extend(data)
        return len(data)

    def flush(self):
        pass

    def read(self, count):
        if not self.chunks:
            raise AssertionError("Unexpected read after completed/failing session")
        chunk = self.chunks.popleft()
        if isinstance(chunk, Exception):
            raise chunk
        if self.after_read:
            self.after_read()
        return chunk


def attach(monkeypatch, tmp_path, console):
    port = tmp_path / "target-port"
    port.touch()

    def factory(**kwargs):
        assert kwargs == {"port": None, "baudrate": 115200, "timeout": 0.1, "write_timeout": 1, "exclusive": True}
        return console

    monkeypatch.setattr(dev_cli.serial, "Serial", factory)
    return port


@pytest.mark.parametrize("command", ["send", "session", "log"])
def test_explicit_port_required_before_any_io(monkeypatch, command):
    def forbidden_open(*args, **kwargs):
        pytest.fail("Missing --port must never attempt device access")

    monkeypatch.setattr(dev_cli.serial, "Serial", forbidden_open)
    result = runner.invoke(dev_cli.app, [command])
    assert result.exit_code == 2
    assert "--port" in result.output


def test_missing_port_fails_without_waiting_or_opening(monkeypatch, tmp_path):
    def forbidden_io(*args, **kwargs):
        pytest.fail("Missing port must fail before opening or sleeping")

    monkeypatch.setattr(dev_cli.serial, "Serial", forbidden_io)
    monkeypatch.setattr(dev_cli.time, "sleep", forbidden_io)
    result = runner.invoke(dev_cli.app, ["session", "s", "--port", str(tmp_path / "absent")])
    assert result.exit_code == 1
    assert "Port does not exist" in result.output


def test_one_open_preserves_response_utf8_and_split_sleep_marker(monkeypatch, tmp_path):
    console = Console(chunks=[b"state: \xe5\xb0", b"\xb1\xe7\xbb\xaa\n[sl", b"eep]\n"])
    port = attach(monkeypatch, tmp_path, console)
    output = tmp_path / "session.log"
    result = runner.invoke(dev_cli.app, ["session", "s", "--port", str(port), "--once", "--save", str(output)])
    assert result.exit_code == 0, result.output
    assert result.output == "state: 就绪\n[sleep]\n"
    assert output.read_bytes() == b"state: \xe5\xb0\xb1\xe7\xbb\xaa\n[sleep]\n"
    assert console.written == b"s"
    assert console.opens == 1 and console.closed


def test_disconnect_preserves_partial_log_and_does_not_resend(monkeypatch, tmp_path):
    console = Console(chunks=[b"refresh started\n", serial.SerialException("device disconnected")])
    port = attach(monkeypatch, tmp_path, console)
    output = tmp_path / "failed.log"
    result = runner.invoke(dev_cli.app, ["send", "r", "--port", str(port), "--save", str(output)])
    assert result.exit_code == 1
    assert "device disconnected" in result.output
    assert output.read_bytes() == b"refresh started\n"
    assert console.written == b"r"
    assert console.opens == 1 and console.closed


def test_disappearing_node_fails_even_if_read_returns_empty(monkeypatch, tmp_path):
    console = Console(chunks=[b""])
    port = attach(monkeypatch, tmp_path, console)
    console.after_read = port.unlink
    result = runner.invoke(dev_cli.app, ["log", "--port", str(port)])
    assert result.exit_code == 1
    assert "Port disconnected" in result.output
    assert console.opens == 1 and console.closed


def test_short_write_stops_before_remaining_commands(monkeypatch, tmp_path):
    console = Console(short_write=True)
    port = attach(monkeypatch, tmp_path, console)
    result = runner.invoke(dev_cli.app, ["send", "rn", "--port", str(port)])
    assert result.exit_code == 1
    assert "Short write sending command byte 1/2" in result.output
    assert console.written == b""
    assert console.closed


def test_captured_pm1_failure_stops_before_reboot_loop(monkeypatch, tmp_path):
    console = Console(chunks=[b"[power] fatal: cannot identify PM1 supply source\r\n"])
    port = attach(monkeypatch, tmp_path, console)
    output = tmp_path / "failed-boot.log"
    result = runner.invoke(dev_cli.app, ["log", "--port", str(port), "--save", str(output)])
    assert result.exit_code == 1
    assert output.read_bytes() == b"[power] fatal: cannot identify PM1 supply source\r\n"
    assert console.closed


def test_boot_and_response_are_read_before_later_command(monkeypatch, tmp_path):
    clock = [0.0]
    console = Console(chunks=[b"boot ready\n", b"state response\n", b"[sleep]\n"])
    observed_writes = []

    def after_read():
        observed_writes.append(bytes(console.written))
        clock[0] += 1

    console.after_read = after_read
    port = attach(monkeypatch, tmp_path, console)
    monkeypatch.setattr(dev_cli.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(dev_cli.time, "monotonic_ns", lambda: int(clock[0] * 1_000_000_000))
    events = tmp_path / "events.jsonl"
    result = runner.invoke(
        dev_cli.app,
        [
            "session",
            "sp",
            "--port",
            str(port),
            "--start-delay",
            "1",
            "--delay",
            "1",
            "--events",
            str(events),
            "--once",
        ],
    )
    assert result.exit_code == 0, result.output
    assert observed_writes == [b"", b"s", b"sp"]
    records = list(map(json.loads, events.read_text().splitlines()))
    assert [(row["command"], row["host_monotonic_ns"]) for row in records] == [
        ("s", 1_000_000_000),
        ("p", 2_000_000_000),
    ]
    assert [(row["clock"], row["label"], row["t_ns"]) for row in records] == [
        ("host-monotonic", "console 's'", 1_000_000_000),
        ("host-monotonic", "console 'p'", 2_000_000_000),
    ]
