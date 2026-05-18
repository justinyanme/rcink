"""Quote of the day: hand-curated short quotes that fit the h1 poster layout.

No external API. The rotation is keyed on the proleptic Gregorian ordinal so it
is deterministic and changes once per local day.

Lines are pre-broken to fit a 400 px wide screen at h1 (Archivo Black /
FreeSans, ~36 px box). Keep each line short — 12 chars is a comfortable cap.
"""

from __future__ import annotations

import datetime as dt
from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class Quote:
    lines: tuple[str, ...]  # 1–4 lines, rendered at h1
    attrib: str  # e.g. "— ALAN KAY"


# Each quote is curated to fit the poster format: very short lines, all caps,
# attribution on its own line below. Edit freely.
QUOTES: tuple[Quote, ...] = (
    Quote(("BUILD", "PROTOCOLS,", "NOT", "PRODUCTS."), "— ALAN KAY"),
    Quote(("MAKE IT WORK,", "MAKE IT", "RIGHT,", "MAKE IT FAST."), "— KENT BECK"),
    Quote(("PREMATURE", "OPTIMIZATION", "IS THE ROOT", "OF ALL EVIL."), "— DONALD KNUTH"),
    Quote(("SIMPLICITY", "IS THE", "ULTIMATE", "SOPHISTICATION."), "— LEONARDO DA VINCI"),
    Quote(("TALK IS CHEAP.", "SHOW ME", "THE CODE."), "— LINUS TORVALDS"),
    Quote(("PROGRAMS MUST", "BE WRITTEN", "FOR PEOPLE", "TO READ."), "— HAL ABELSON"),
    Quote(("FIRST, SOLVE", "THE PROBLEM.", "THEN, WRITE", "THE CODE."), "— JOHN JOHNSON"),
    Quote(("DEBUGGING IS", "TWICE AS HARD", "AS WRITING", "THE CODE."), "— BRIAN KERNIGHAN"),
    Quote(("ANY FOOL CAN", "WRITE CODE", "A COMPUTER", "CAN UNDERSTAND."), "— MARTIN FOWLER"),
    Quote(("THE BEST WAY", "TO PREDICT", "THE FUTURE", "IS TO INVENT IT."), "— ALAN KAY"),
    Quote(("LESS IS MORE.",), "— MIES VAN DER ROHE"),
    Quote(("MOVE FAST", "WITH STABLE", "INFRASTRUCTURE."), "— MARK ZUCKERBERG"),
    Quote(("WHAT I CANNOT", "CREATE,", "I DO NOT", "UNDERSTAND."), "— RICHARD FEYNMAN"),
    Quote(("PERFECTION IS", "ACHIEVED WHEN", "THERE IS NOTHING", "LEFT TO TAKE AWAY."), "— SAINT-EXUPÉRY"),
    Quote(("CODE NEVER LIES.", "COMMENTS", "SOMETIMES DO."), "— RON JEFFRIES"),
    Quote(("WALKING ON WATER", "AND BUILDING", "SOFTWARE FROM A SPEC", "ARE EASY..."), "— EDWARD V. BERARD"),
    Quote(("THE COMPUTER", "WAS BORN", "TO SOLVE PROBLEMS", "THAT DIDN'T EXIST."), "— BILL GATES"),
    Quote(("FOCUS IS", "ABOUT SAYING", "NO."), "— STEVE JOBS"),
    Quote(("DO ONE THING", "AND DO IT", "WELL."), "— UNIX PHILOSOPHY"),
    Quote(("EVERYTHING SHOULD", "BE MADE AS", "SIMPLE AS", "POSSIBLE."), "— ALBERT EINSTEIN"),
    Quote(("MEASURE TWICE,", "CUT ONCE."), "— CARPENTER'S RULE"),
)


def pick_quote(today: dt.date) -> Quote:
    """Pick a deterministic quote for the given date.

    Same date → same quote everywhere. Rolls over at local midnight.
    """
    return QUOTES[today.toordinal() % len(QUOTES)]
