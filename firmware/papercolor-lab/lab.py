#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["typer>=0.12", "pyserial>=3.5", "pillow>=11"]
# ///
"""PaperColor Lab host: one Trial = one console session into one new directory.

Port and camera are explicit; nothing is discovered, reopened, resent or retried.
"""

import json
import os
import shutil
import subprocess
import time
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Annotated, BinaryIO, Protocol

import serial
import typer
from PIL import Image

LAB = Path(__file__).resolve().parent
ROOT = LAB.parents[1]
RCEYE = ROOT.parent / "rceye"
FLASH_TOOL = ROOT / "scripts/papercolor_flash.py"
LAB_BUILD = LAB / ".pio/build/m5stack-papercolor-lab"
BANNER = "# ready "
FATAL_MARKERS = (b"[boot] fatal:", b"Guru Meditation", b"abort() was called")
EVIDENCE_LONG_EDGE = 1600
EVIDENCE_MAX_BYTES = 200_000


class TrialError(RuntimeError):
    """A bench anomaly: reported loudly, never retried."""


class Link(Protocol):
    def read(self, size: int) -> bytes: ...
    def write(self, data: bytes) -> int | None: ...
    def flush(self) -> None: ...
    def close(self) -> None: ...


Runner = Callable[..., subprocess.CompletedProcess]


@dataclass(slots=True)
class Result:
    ok: bool
    fields: dict[str, str]
    line: str


def parse_result(line: str) -> Result | None:
    """`OK k=v ...` / `FAIL k=v ...` → Result; any other line → None."""
    head, *tokens = line.split()
    if head not in ("OK", "FAIL"):
        return None
    fields = {}
    for token in tokens:
        key, sep, value = token.partition("=")
        if not sep or not key:
            raise TrialError(f"Malformed result field {token!r} in {line!r}")
        fields[key] = value
    return Result(ok=head == "OK", fields=fields, line=line)


def parse_steps(text: str) -> list[str]:
    steps = [step.strip() for step in text.split(",")]
    if not steps or any(not step for step in steps):
        raise typer.BadParameter(f"Empty step in {text!r}")
    return steps


def open_serial(port: str) -> serial.Serial:
    link = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1, exclusive=True)
    link.dtr = False
    link.rts = False
    link.port = port
    link.open()
    return link


def slug(command: str) -> str:
    return "-".join(command.replace("=", "").split()) or "idle"


@dataclass(slots=True)
class Session:
    """One open console plus the Trial directory it writes into."""

    link: Link
    port: str
    out: Path
    camera: str
    run: Runner
    log: BinaryIO
    events: BinaryIO
    pending: bytearray = field(default_factory=bytearray)
    photos: int = 0
    alive: bool = True

    def event(self, kind: str, **data: object) -> None:
        record = {"t_ns": time.monotonic_ns(), "clock": "host-monotonic", "event": kind, **data}
        self.events.write((json.dumps(record, ensure_ascii=False) + "\n").encode())
        self.events.flush()

    def read_line(self, deadline: float, waiting_for: str) -> str:
        while True:
            if (end := self.pending.find(b"\n")) >= 0:
                line = self.pending[:end].decode(errors="replace").rstrip("\r")
                del self.pending[: end + 1]
                return line
            if time.monotonic() >= deadline:
                raise TrialError(f"No {waiting_for} before timeout; partial={bytes(self.pending)!r}")
            try:
                data = self.link.read(4096)
            except serial.SerialException as error:
                self.alive = False
                raise TrialError(f"Port {self.port} vanished: {error}") from error
            if not data:
                continue
            self.log.write(data)
            self.log.flush()
            print(data.decode(errors="replace"), end="", flush=True)
            self.pending.extend(data)
            if marker := next((m for m in FATAL_MARKERS if m in self.pending), None):
                raise TrialError(f"Firmware fatal {marker.decode()}; see serial.log")

    def wait_banner(self, seconds: float) -> str:
        deadline = time.monotonic() + seconds
        while not (line := self.read_line(deadline, "boot banner")).startswith(BANNER):
            pass
        self.event("boot", line=line)
        return line

    def command(self, line: str, seconds: float) -> Result:
        payload = (line + "\n").encode("ascii")
        self.event("write", line=line)
        try:
            written = self.link.write(payload)
            self.link.flush()
        except serial.SerialException as error:
            self.alive = False
            raise TrialError(f"Port {self.port} vanished on write: {error}") from error
        if written != len(payload):
            raise TrialError(f"Short write: {written}/{len(payload)} bytes of {line!r}")
        deadline = time.monotonic() + seconds
        while (result := parse_result(self.read_line(deadline, f"result for {line!r}"))) is None:
            pass
        self.event("result", line=result.line, ok=result.ok, fields=result.fields)
        if not result.ok:
            raise TrialError(f"{line!r} → {result.line}")
        return result

    def photo(self, name: str) -> Path:
        self.photos += 1
        path = self.out / f"{self.photos:02d}-{name}.jpg"
        env = {key: value for key, value in os.environ.items() if key != "VIRTUAL_ENV"}
        argv = ["uv", "run", "--project", str(RCEYE), "eye", "--eye", self.camera, "photo", "--out", str(path)]
        done = self.run(argv, check=True, timeout=60, capture_output=True, text=True, env=env)
        if not path.is_file():
            raise TrialError(f"Camera reported success but {path} is missing")
        self.event("photo", file=path.name, camera=self.camera, stdout=done.stdout.strip())
        return path


def run_trial(
    *,
    out: Path,
    port: str,
    camera: str,
    steps: list[str],
    restore: bool = True,
    step_timeout: float = 60.0,
    boot_timeout: float = 5.0,
    open_link: Callable[[str], Link] = open_serial,
    run: Runner = subprocess.run,
    port_exists: Callable[[str], bool] = lambda port: Path(port).exists(),
) -> Path:
    if not port_exists(port):
        raise TrialError(f"Port absent: {port}; no discovery")
    out.mkdir(parents=True, exist_ok=False)
    with (out / "serial.log").open("wb") as log, (out / "events.jsonl").open("wb") as events:
        link = open_link(port)
        session = Session(link=link, port=port, out=out, camera=camera, run=run, log=log, events=events)
        session.event("open", port=port, camera=camera, steps=steps, restore=restore)
        shown = False
        try:
            # Opening the native USB console resets the unit: wait for its banner.
            session.wait_banner(boot_timeout)
            last = "idle"
            for step in steps:
                if step == "photo":
                    session.photo(slug(last))
                    continue
                shown |= step.split()[0] == "show"
                last = step
                session.command(step, step_timeout)
            if restore and shown:
                session.command("cleanup", step_timeout)
                session.photo("cleanup")
        except Exception as error:
            session.event("error", message=str(error))
            if restore and shown and session.alive:
                # One best-effort Native refresh so the panel is left known.
                try:
                    session.command("cleanup", step_timeout)
                    session.photo("cleanup-after-failure")
                except Exception as cleanup_error:
                    session.event("error", message=f"cleanup failed: {cleanup_error}")
            raise
        finally:
            link.close()
    return out


def curate(src: Path, dest: Path) -> list[Path]:
    """Copy a Trial into evidence: logs verbatim, photos ≤1600 px and ≤200 KB."""
    if not (src / "serial.log").is_file():
        raise TrialError(f"{src} is not a Trial directory (no serial.log)")
    dest.mkdir(parents=True, exist_ok=False)
    written = [Path(shutil.copy2(src / name, dest / name)) for name in ("serial.log", "events.jsonl")]
    for photo in sorted(src.glob("*.jpg")):
        image = Image.open(photo)
        image.thumbnail((EVIDENCE_LONG_EDGE, EVIDENCE_LONG_EDGE))
        target = dest / photo.name
        for quality in range(85, 30, -5):
            image.save(target, "JPEG", quality=quality, optimize=True)
            if target.stat().st_size <= EVIDENCE_MAX_BYTES:
                break
        else:
            raise TrialError(f"{photo.name} stays over {EVIDENCE_MAX_BYTES} B at quality 35")
        written.append(target)
    return written


app = typer.Typer(pretty_exceptions_show_locals=False, no_args_is_help=True)


@app.command()
def trial(
    out: Annotated[Path, typer.Argument(help="New Trial directory (must not exist)")],
    port: Annotated[str, typer.Option(help="Exact Lab console port")],
    camera: Annotated[str, typer.Option(help="rceye Eye, e.g. android:SERIAL")],
    steps: Annotated[str, typer.Option(help='Comma list of console lines and "photo"')],
    restore: Annotated[bool, typer.Option(help="Finish with cleanup + photo when anything was shown")] = True,
    step_timeout: Annotated[float, typer.Option(help="Seconds per console result")] = 60.0,
    flash: Annotated[bool, typer.Option(help="Flash the built Lab firmware first (manifest in OUT-flash)")] = False,
) -> None:
    if flash:
        flash_firmware(out.with_name(out.name + "-flash"), port)
    try:
        run_trial(
            out=out, port=port, camera=camera, steps=parse_steps(steps), restore=restore, step_timeout=step_timeout
        )
    except TrialError as error:
        typer.echo(f"Trial failed: {error}", err=True)
        raise typer.Exit(1) from error
    typer.echo(f"Trial: {out}")


def flash_firmware(out: Path, port: str) -> None:
    subprocess.run(
        ["uv", "run", str(FLASH_TOOL), "--port", port, "--out", str(out), "--build-dir", str(LAB_BUILD)],
        check=True,
    )


@app.command()
def flash(
    out: Annotated[Path, typer.Argument(help="New directory for the flash manifest")],
    port: Annotated[str, typer.Option(help="Exact PaperColor port")],
) -> None:
    """MAC-checked app-only flash of the built Lab firmware (scripts/papercolor_flash.py)."""
    flash_firmware(out, port)


@app.command("curate")
def curate_command(
    src: Annotated[Path, typer.Argument(help="Trial directory (.cache)")],
    dest: Annotated[Path, typer.Argument(help="New evidence directory")],
) -> None:
    for path in curate(src, dest):
        typer.echo(f"{path} {path.stat().st_size}")


if __name__ == "__main__":
    app()
