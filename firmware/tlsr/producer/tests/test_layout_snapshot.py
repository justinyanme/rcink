import json
from pathlib import Path

from tlsr_layout.pages.example import example_page

GOLDEN = Path(__file__).parent / "golden" / "example_page.json"


def test_example_page_matches_golden():
    actual = example_page()
    expected = json.loads(GOLDEN.read_text())
    assert actual == expected
