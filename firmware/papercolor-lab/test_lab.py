#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["pytest>=8", "typer>=0.12", "pyserial>=3.5", "pillow>=11"]
# ///
"""lab.py against recorded console transcripts and a recording camera."""

import json
import sys
from pathlib import Path

import pytest
import serial
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lab

PORT = "/dev/cu.usbmodem83301"
CAMERA = "android:3B161600Z2U00000"

# Captured on hardware: evidence/20261015-skeleton-001/serial.log (boot + replies).
BOOT = (
    b"ESP-ROM:esp32s3-20210327\r\nBuild:Mar 27 2021\r\n"
    b"rst:0x15 (USB_UART_CHIP_RESET),boot:0x2b (SPI_FAST_FLASH_BOOT)\r\nentry 0x403c98d0\r\n"
    b"# ready build=9e7f968da28e-dirty mac=44:1b:f6:c1:31:90 rev=01010400 supply=0x05 uptime_ms=246\r\n"
)
SHOW_BLACK_OK = b"OK image=black mode=native busy_ms=14389 total_ms=16137\r\n"
CLEANUP_OK = b"OK mode=native busy_ms=14389 total_ms=16137\r\n"
DRIVE_FAIL = b"FAIL stage=drive-release busy=0 ms=25000\r\n"
VANISH = object()


class FakeLink:
    """Replays a transcript: boot bytes, then per expected write its reply."""

    def __init__(self, boot: bytes, replies: dict[str, object]) -> None:
        self.buffer = bytearray(boot)
        self.replies = replies
        self.writes: list[bytes] = []
        self.vanished = False
        self.closed = False

    def read(self, size: int) -> bytes:
        if self.vanished:
            raise serial.SerialException("device reports readiness to read but returned no data")
        chunk = bytes(self.buffer[:size])
        del self.buffer[:size]
        return chunk

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        reply = self.replies[data.decode().strip()]
        if reply is VANISH:
            self.vanished = True
        else:
            self.buffer.extend(reply)
        return len(data)

    def flush(self) -> None:
        pass

    def close(self) -> None:
        self.closed = True


class Camera:
    def __init__(self) -> None:
        self.argvs: list[list[str]] = []

    def __call__(self, argv: list[str], **kwargs: object):
        self.argvs.append(argv)
        out = Path(argv[argv.index("--out") + 1])
        Image.new("RGB", (64, 48), "white").save(out)
        return lab.subprocess.CompletedProcess(argv, 0, stdout="3072x3072 1 B 1 ms\n", stderr="")


def trial(tmp_path: Path, link: FakeLink, steps: str, **kwargs: object) -> tuple[Path, Camera, list[str]]:
    opens: list[str] = []
    camera = Camera()

    def open_link(port: str) -> FakeLink:
        opens.append(port)
        return link

    out = tmp_path / "t"
    try:
        lab.run_trial(
            out=out,
            port=PORT,
            camera=CAMERA,
            steps=lab.parse_steps(steps),
            step_timeout=0.2,
            boot_timeout=0.2,
            open_link=open_link,
            run=camera,
            port_exists=lambda port: True,
            **kwargs,
        )
    finally:
        assert opens == [PORT] or opens == []
    return out, camera, opens


def events(out: Path) -> list[dict]:
    return [json.loads(line) for line in (out / "events.jsonl").read_text().splitlines()]


def eye_argv(path: Path) -> list[str]:
    return ["uv", "run", "--project", str(lab.RCEYE), "eye", "--eye", CAMERA, "photo", "--out", str(path)]


def test_ok_trial_restores_and_records(tmp_path: Path) -> None:
    link = FakeLink(BOOT, {"show black native": SHOW_BLACK_OK, "cleanup": CLEANUP_OK})
    out, camera, opens = trial(tmp_path, link, "show black native,photo")
    assert opens == [PORT] and link.closed
    assert link.writes == [b"show black native\n", b"cleanup\n"]
    assert sorted(p.name for p in out.iterdir()) == [
        "01-show-black-native.jpg",
        "02-cleanup.jpg",
        "events.jsonl",
        "serial.log",
    ]
    assert (out / "serial.log").read_bytes() == BOOT + SHOW_BLACK_OK + CLEANUP_OK
    assert camera.argvs == [eye_argv(out / "01-show-black-native.jpg"), eye_argv(out / "02-cleanup.jpg")]
    kinds = [(e["event"], e.get("line") or e.get("file")) for e in events(out)]
    assert kinds == [
        ("open", None),
        ("boot", BOOT.decode().splitlines()[-1]),
        ("write", "show black native"),
        ("result", "OK image=black mode=native busy_ms=14389 total_ms=16137"),
        ("photo", "01-show-black-native.jpg"),
        ("write", "cleanup"),
        ("result", "OK mode=native busy_ms=14389 total_ms=16137"),
        ("photo", "02-cleanup.jpg"),
    ]
    result = events(out)[3]
    assert result["ok"] is True and result["fields"]["busy_ms"] == "14389"


def test_fail_result_stops_after_one_cleanup(tmp_path: Path) -> None:
    link = FakeLink(BOOT, {"show black native": DRIVE_FAIL, "cleanup": CLEANUP_OK})
    with pytest.raises(lab.TrialError, match="stage=drive-release"):
        trial(tmp_path, link, "show black native,photo,show white native")
    out = tmp_path / "t"
    assert link.writes == [b"show black native\n", b"cleanup\n"]
    assert [p.name for p in out.glob("*.jpg")] == ["01-cleanup-after-failure.jpg"]
    failed = [e for e in events(out) if e["event"] == "result"][0]
    assert failed["ok"] is False and failed["fields"] == {"stage": "drive-release", "busy": "0", "ms": "25000"}


def test_missing_result_times_out_without_resend(tmp_path: Path) -> None:
    link = FakeLink(BOOT, {"show black native": b"# progress only\r\n", "cleanup": CLEANUP_OK})
    with pytest.raises(lab.TrialError, match="No result for 'show black native'"):
        trial(tmp_path, link, "show black native")
    assert link.writes == [b"show black native\n", b"cleanup\n"]


def test_port_vanishing_mid_session_is_not_reopened(tmp_path: Path) -> None:
    link = FakeLink(BOOT, {"show black native": VANISH})
    with pytest.raises(lab.TrialError, match="vanished"):
        trial(tmp_path, link, "show black native,photo")
    out = tmp_path / "t"
    assert link.writes == [b"show black native\n"] and link.closed
    assert not list(out.glob("*.jpg"))
    assert events(out)[-1]["event"] == "error"


def test_missing_banner_sends_nothing(tmp_path: Path) -> None:
    link = FakeLink(BOOT.replace(b"# ready", b"# reboot"), {})
    with pytest.raises(lab.TrialError, match="boot banner"):
        trial(tmp_path, link, "show black native")
    assert link.writes == []


def test_firmware_fatal_is_reported(tmp_path: Path) -> None:
    link = FakeLink(b"[boot] fatal: PM1 absent\r\n", {})
    with pytest.raises(lab.TrialError, match="fatal"):
        trial(tmp_path, link, "info")
    assert link.writes == []


def test_no_restore_skips_cleanup(tmp_path: Path) -> None:
    link = FakeLink(BOOT, {"show black native": SHOW_BLACK_OK})
    out, camera, _ = trial(tmp_path, link, "show black native,photo", restore=False)
    assert link.writes == [b"show black native\n"]
    assert len(camera.argvs) == 1


def test_existing_directory_is_refused_before_open(tmp_path: Path) -> None:
    (tmp_path / "t").mkdir()
    link = FakeLink(BOOT, {})
    with pytest.raises(FileExistsError):
        trial(tmp_path, link, "info")
    assert link.writes == []


def test_parse_result_on_captured_lines() -> None:
    line = "OK mac=44:1b:f6:c1:31:90 rev=01010400 build=9e7f968da28e-dirty temp=2080 uptime_ms=328"
    result = lab.parse_result(line)
    assert result is not None and result.ok
    assert result.fields == {
        "mac": "44:1b:f6:c1:31:90",
        "rev": "01010400",
        "build": "9e7f968da28e-dirty",
        "temp": "2080",
        "uptime_ms": "328",
    }
    assert lab.parse_result("# ready build=x") is None
    assert lab.parse_result("entry 0x403c98d0") is None
    with pytest.raises(lab.TrialError):
        lab.parse_result("OK busy_ms")


def test_curate_downsizes_photos(tmp_path: Path) -> None:
    src = tmp_path / "src"
    src.mkdir()
    (src / "serial.log").write_bytes(BOOT)
    (src / "events.jsonl").write_text("{}\n")
    Image.radial_gradient("L").resize((3072, 2304)).convert("RGB").save(src / "01-x.jpg", quality=95)
    written = lab.curate(src, tmp_path / "dest")
    photo = Image.open(tmp_path / "dest/01-x.jpg")
    assert photo.size == (1600, 1200)
    assert (tmp_path / "dest/01-x.jpg").stat().st_size <= 200_000
    assert (tmp_path / "dest/serial.log").read_bytes() == BOOT
    assert len(written) == 3


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q", "-p", "no:cacheprovider"]))
