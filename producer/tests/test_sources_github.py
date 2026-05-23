from rcpaper.sources.github import (
    GRID_CELLS,
    LangWeek,
    RepoWeek,
    _aggregate_languages,
    _compute_streak,
    _normalize,
    _short_repo_name,
)


def _days(*counts: int) -> list[tuple[str, int]]:
    # Dates don't matter for streak/normalize — only the order.
    return [(f"2026-01-{i + 1:02d}", c) for i, c in enumerate(counts)]


def test_streak_simple_run_to_today():
    # Three contribution days, ending today (last entry > 0).
    assert _compute_streak(_days(0, 0, 1, 2, 3)) == 3


def test_streak_today_zero_grace():
    # Today is 0 — streak should still count the prior run.
    assert _compute_streak(_days(1, 2, 3, 0)) == 3


def test_streak_broken_run():
    # 0 between runs breaks the streak; only the trailing run counts.
    assert _compute_streak(_days(5, 5, 0, 2, 3, 4)) == 3


def test_streak_no_contributions():
    assert _compute_streak(_days(0, 0, 0)) == 0


def test_streak_empty():
    assert _compute_streak([]) == 0


def test_normalize_scales_to_max():
    assert _normalize([0, 1, 2, 4]) == [0, 25, 50, 100]


def test_normalize_all_zero():
    assert _normalize([0, 0, 0]) == [0, 0, 0]


def test_normalize_empty():
    assert _normalize([]) == []


def test_grid_cells_default():
    # 14 weeks × 7 days. If this changes, the grid primitive on the device
    # needs an aligned width/height tweak.
    assert GRID_CELLS == 98


def test_short_repo_name_strips_own_owner():
    assert _short_repo_name("rocry/rcpapercolor", user="rocry") == "rcpapercolor"


def test_short_repo_name_keeps_foreign_owner():
    assert _short_repo_name("m5stack/M5GFX", user="rocry") == "m5stack/M5GFX"


def test_aggregate_languages_weighted_by_commits():
    # Repo A: 8 commits, Go=800 bytes, Python=200 bytes.
    # Repo B: 2 commits, Go=0,   Python=1000 bytes.
    # Commit-weighted size: Go = 8*800 = 6400, Python = 8*200 + 2*1000 = 3600.
    # Total = 10000; pcts = Go 64, Python 36.
    repos = [
        ("rocry/a", 8, [("Go", 800), ("Python", 200)]),
        ("rocry/b", 2, [("Python", 1000)]),
    ]
    langs = _aggregate_languages(repos)
    by_name = {l.name: l.pct for l in langs}
    assert by_name["Go"] == 64
    assert by_name["Python"] == 36


def test_aggregate_languages_empty():
    assert _aggregate_languages([]) == []


def test_aggregate_languages_caps_to_top_five():
    repos = [
        ("rocry/a", 1, [(f"L{i}", 1000 - i) for i in range(8)]),
    ]
    langs = _aggregate_languages(repos)
    assert len(langs) == 5
    assert [l.name for l in langs] == ["L0", "L1", "L2", "L3", "L4"]


def test_lang_week_and_repo_week_exported():
    assert RepoWeek(name="x", commits=1).commits == 1
    assert LangWeek(name="Go", pct=10).pct == 10
