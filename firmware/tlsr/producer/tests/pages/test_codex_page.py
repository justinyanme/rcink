import json
from pathlib import Path

from tlsr_layout.pages.codex import build_page
from tlsr_layout.sources.codex import CodexUsage

from .test_colors import assert_bwr_only

FIXTURES = Path(__file__).parent / "fixtures"
GOLDEN = Path(__file__).parent / "golden"


def test_codex_page_matches_golden():
    usage = CodexUsage(**json.loads((FIXTURES / "codex.json").read_text()))

    page = build_page(usage)

    assert page == json.loads((GOLDEN / "codex.json").read_text())
    assert_bwr_only(page)
