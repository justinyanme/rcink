"""Golden PNG regression test.

Re-renders each (device, page) pair and asserts byte-for-byte match with
dsl/golden/<device>/<page>.png. To update goldens deliberately, run:

    uv run rcink-goldens regen

Then commit the new PNGs alongside the producer change.
"""

from pathlib import Path

import pytest
from rcink.cli_goldens import _BUILDERS, DEVICES, _layout_of
from rcink.preview_png import render_to_png, unsupported_node_type

GOLDEN_ROOT = Path(__file__).resolve().parent.parent.parent / "dsl" / "golden"

_pairs = [(prof, page) for prof in DEVICES for page in _BUILDERS]


@pytest.mark.parametrize("prof,page", _pairs, ids=[f"{p.name}-{pg}" for p, pg in _pairs])
def test_golden_matches(prof, page, tmp_path):
    layout = _layout_of(_BUILDERS[page]())
    unsupported = unsupported_node_type(layout, prof)
    if unsupported is not None:
        pytest.skip(f"{prof.name} does not support {unsupported}")

    golden = GOLDEN_ROOT / prof.name / f"{page}.png"
    assert golden.exists(), f"missing golden {golden}; run `uv run rcink-goldens regen`"
    img = render_to_png(layout, prof)

    out = tmp_path / "actual.png"
    img.save(out)
    assert out.read_bytes() == golden.read_bytes(), (
        f"golden drift for {prof.name}/{page}.png — if intentional, run `uv run rcink-goldens regen` and recommit."
    )
