from rcpaper.sources.github import GRID_CELLS, _compute_streak, _normalize


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
