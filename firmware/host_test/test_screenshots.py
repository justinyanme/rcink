from pathlib import Path

import pytest

from conftest import ROOT, run_render

SCENARIOS = sorted((ROOT / "scenarios").glob("*.json"))


@pytest.mark.parametrize("scenario", SCENARIOS, ids=[s.stem for s in SCENARIOS])
def test_screenshot_writes(scenario: Path, out_dir: Path):
    pgm = out_dir / f"{scenario.stem}.pgm"
    png = out_dir / f"{scenario.stem}.png"
    run_render(scenario, pgm, png)
    assert pgm.exists() and pgm.stat().st_size > 0
    assert png.exists() and png.stat().st_size > 0
