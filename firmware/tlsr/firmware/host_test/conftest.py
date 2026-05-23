import json
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).parent
BIN = ROOT / "build" / "host_test"


def pytest_configure(config):
    if not BIN.exists():
        raise RuntimeError(f"host_test binary not built at {BIN}. Run `cmake -S . -B build && cmake --build build`.")


@pytest.fixture(scope="session")
def out_dir() -> Path:
    d = ROOT / "out"
    if d.exists():
        shutil.rmtree(d)
    d.mkdir()
    return d


def run_layout(scenario: Path) -> list[dict]:
    res = subprocess.run([BIN, "layout", str(scenario)], capture_output=True, check=True)
    return json.loads(res.stdout)


def run_render(scenario: Path, pgm_path: Path, png_path: Path | None = None) -> None:
    args = [BIN, "render", str(scenario), "--pgm", str(pgm_path)]
    if png_path:
        args += ["--png", str(png_path)]
    subprocess.run(args, capture_output=True, check=True)
