import datetime as dt

from rcpaper.sources.quotes import QUOTES, Quote, pick_quote


def test_rotation_is_deterministic_per_date():
    d = dt.date(2026, 5, 18)
    assert pick_quote(d) is pick_quote(d)


def test_rotation_changes_day_to_day():
    # Step a window of ~len(QUOTES) days; at least one neighboring pair must differ.
    base = dt.date(2026, 5, 18)
    picks = [pick_quote(base + dt.timedelta(days=i)) for i in range(len(QUOTES))]
    assert any(picks[i] is not picks[i + 1] for i in range(len(picks) - 1))


def test_every_quote_appears_in_one_full_cycle():
    base = dt.date(2026, 5, 18)
    picks = {pick_quote(base + dt.timedelta(days=i)) for i in range(len(QUOTES))}
    assert picks == set(QUOTES)


def test_quotes_are_well_formed():
    for q in QUOTES:
        assert isinstance(q, Quote)
        assert 1 <= len(q.lines) <= 4, f"too many lines: {q}"
        assert all(isinstance(line, str) and line for line in q.lines)
        assert q.attrib.startswith("—") or q.attrib.startswith("-"), q.attrib
        # h1 lines should stay short enough to fit the 400 px screen.
        assert all(len(line) <= 24 for line in q.lines), f"line too long: {q}"
