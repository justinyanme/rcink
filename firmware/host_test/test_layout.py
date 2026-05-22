import json
from pathlib import Path

import pytest
from conftest import ROOT, run_layout

SCENARIOS = sorted((ROOT / "scenarios").glob("*.json"))


@pytest.mark.parametrize("scenario", SCENARIOS, ids=[s.stem for s in SCENARIOS])
def test_layout_matches_golden(scenario: Path):
    actual = run_layout(scenario)
    golden_path = ROOT / "golden" / f"{scenario.stem}.layout.json"
    if not golden_path.exists():
        pytest.skip(f"no golden: {golden_path}")
    expected = json.loads(golden_path.read_text())
    assert actual == expected


def test_negative_pct_clamps_to_zero(tmp_path: Path):
    scenario = tmp_path / "edge_negative_pct.json"
    scenario.write_text(json.dumps({"type": "hbar", "pct": -5, "color": "red", "height": 8}))
    laid = run_layout(scenario)
    assert len(laid) == 1
    assert laid[0]["kind"] == "hbar"
