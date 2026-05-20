import subprocess
import zlib
from pathlib import Path

import pytest

from conftest import ROOT

BIN = ROOT / "build" / "host_test"
SCENARIOS = sorted((ROOT / "scenarios").glob("*.json"))


@pytest.mark.parametrize("scenario", SCENARIOS, ids=[s.stem for s in SCENARIOS])
def test_e2e_inflate_parse_render(scenario: Path, out_dir: Path):
    raw = scenario.read_bytes()
    compressed = zlib.compress(raw, level=9)
    out_png = out_dir / f"{scenario.stem}.e2e.png"
    subprocess.run(
        [BIN, "pipeline-e2e", "--uncompressed-len", str(len(raw)), "--png", str(out_png)],
        input=compressed,
        check=True,
    )
    assert out_png.exists() and out_png.stat().st_size > 0
