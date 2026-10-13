#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["typer>=0.12"]
# ///
"""Pre-arm the P4 camera, then capture one PaperColor command/log session."""

import json
import re
import signal
import subprocess
import time
from collections import Counter
from pathlib import Path
from typing import Annotated

import typer

ROOT = Path(__file__).resolve().parents[1]


def main(
    port: Annotated[str, typer.Option(help="Exact PaperColor port; no retry/discovery.")],
    out: Annotated[Path, typer.Option(help="New evidence directory.")],
    commands: str = "ps",
    seconds: Annotated[float, typer.Option(min=10)] = 45,
    start_delay: Annotated[float, typer.Option(min=0)] = 2,
) -> None:
    if start_delay >= seconds - 3:
        raise typer.BadParameter("start-delay must leave time for commands before capture ends")
    if not commands or any(command not in "psdrnbfhq" for command in commands):
        raise typer.BadParameter("Observation supports only p/s/d/r/n/b/f/h/q commands")
    if not Path(port).exists():
        raise typer.BadParameter(f"Port absent: {port}; wake the board before observing")
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    video = out / "video"
    with (out / "camera.log").open("w") as camera_log:
        camera = subprocess.Popen(
            [
                "uv",
                "run",
                "--project",
                str(ROOT.parent / "rceye"),
                "eye",
                "--eye",
                "p4",
                "video",
                str(seconds),
                "--size",
                "540p",
                "--keep-frames",
                "--out",
                str(video),
            ],
            stdout=camera_log,
            stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 15
            while True:
                if camera.poll() is not None:
                    raise RuntimeError(f"Camera exited before readiness; see {out / 'camera.log'}")
                ready = False
                for timeline in video.glob("*.frames.jsonl"):
                    with timeline.open() as stream:
                        first = stream.readline()
                    if first.endswith("\n"):
                        json.loads(first)
                        ready = True
                        break
                if ready:
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError("Camera produced no complete timeline record within 15 s")
                time.sleep(0.1)
            print("P4 recording ready; opening the single PaperColor session", flush=True)
            subprocess.run(
                [
                    "uv",
                    "run",
                    "--project",
                    str(ROOT / "producer"),
                    "dev-cli",
                    "session",
                    commands,
                    "--port",
                    port,
                    "--start-delay",
                    str(start_delay),
                    "--timeout",
                    str(seconds - 3),
                    "--save",
                    str(out / "serial.log"),
                    "--events",
                    str(out / "events.jsonl"),
                ],
                check=True,
                timeout=seconds + 10,
            )
            if camera.wait(timeout=30) != 0:
                raise RuntimeError(f"Camera failed; see {out / 'camera.log'}")
            log = (out / "serial.log").read_text(errors="replace")
            completed = Counter(re.findall(r"\[console\] done (.) uptime_ms=\d+", log))
            missing = Counter(commands) - completed
            if missing:
                raise RuntimeError(f"Firmware commands did not complete: {dict(missing)}; inspect serial.log")
            if "fatal:" in log or "Guru Meditation" in log:
                raise RuntimeError("Firmware reported a fatal error; inspect serial.log")
        finally:
            if camera.poll() is None:
                camera.send_signal(signal.SIGINT)
                try:
                    camera.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    camera.kill()
                    camera.wait()
    print(f"Evidence: {out}; inspect original frames, serial log and command timeline")


if __name__ == "__main__":
    typer.run(main)
