import json
from pathlib import Path

from tlsr_layout.pages.claude import build_page
from tlsr_layout.sources.claude import ClaudeUsage

from .test_colors import assert_bwr_only

FIXTURES = Path(__file__).parent / "fixtures"
GOLDEN = Path(__file__).parent / "golden"


def test_claude_page_matches_golden():
    usage = ClaudeUsage(**json.loads((FIXTURES / "claude.json").read_text()))

    page = build_page(usage)

    assert page == json.loads((GOLDEN / "claude.json").read_text())
    assert_bwr_only(page)
