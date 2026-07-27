"""Local card cache + KP3 frame rendering for the KEYPOINT keyboard display.

The wire grammar is not this module's to declare: `rcink.kp3` is the vendored
copy of the keyboard firmware's contract module, and every width, ceiling, icon
spelling and LED code comes from there. This module owns rcink's *card* shape
on top of it — the cache format, the row vocabulary, and which line goes where.

Screen convention: L1 is the card title, the remaining lines are content rows
(the last may be empty). A ``Row(bar=N)`` renders as the firmware's ``[NNN]``
progress token instead of text; ``Row(until=epoch)`` renders as a countdown
computed at push time.

Fetchers drop one compact card per source into the cache dir (atomic replace,
one file per card — no cross-process locking needed); the keypoint-push job
reads the cache and renders frames at push time so countdowns stay live between
15-minute fetches.
"""

from __future__ import annotations

import datetime as dt
import json
import os
from dataclasses import asdict, dataclass, field
from pathlib import Path

from rcink import kp3

# Custom GATT service on the left keyboard half (encrypted write); see the
# KEYPOINT firmware widgets/live_data.{h,c}.
SERVICE_UUID = "f5d40000-6d2f-4f4b-9b2a-2f4a8e8c0001"
CHAR_UUID = "f5d40001-6d2f-4f4b-9b2a-2f4a8e8c0001"

#: rcink's card convention, not contract: L1 is the title, the rest are rows.
MAX_ROWS = kp3.TEXT_LINE_COUNT - 1

# Producer-facing names for the firmware's LED-hint enum; a renamed hint raises
# here at import rather than silently shifting every card's severity.
_LED_CODE = dict(zip(kp3.LED_HINT_NAMES, kp3.LED_CODES, strict=True))
LED_HINT_NONE = _LED_CODE["NONE"]
LED_HINT_ACTIVE = _LED_CODE["ACTIVE"]
LED_HINT_ATTENTION = _LED_CODE["ATTENTION"]
LED_HINT_WARNING = _LED_CODE["WARNING"]
LED_HINT_ERROR = _LED_CODE["ERROR"]

# Rotation order on the keyboard; unknown card ids sort after these, alphabetically.
CARD_ORDER = ("claude", "codex", "weather", "calendar", "todo")

CACHE_ENV = "RCINK_KEYPOINT_CACHE"
CACHE_DEFAULT = "~/.cache/rcink/keypoint"


@dataclass(slots=True, frozen=True)
class Row:
    """One display line: a kv text row (value or until countdown) or a progress
    bar row (bar), or an unpadded text-only row. Exactly one payload is set.

    Bar rows encode a 0-100 utilization percentage as ``[NNN]``; the firmware
    detects this pattern and renders a filled rectangle instead of text."""

    label: str = ""
    value: str | None = None
    until: int | None = None
    bar: int | None = None
    text: str | None = None

    def __post_init__(self) -> None:
        if sum(x is not None for x in (self.value, self.until, self.bar, self.text)) != 1:
            raise ValueError(f"row {self.label!r}: exactly one of value/until/bar/text required")
        if self.text is not None and self.label:
            raise ValueError("text-only row cannot also have a label")
        if self.bar is not None and not 0 <= self.bar <= 100:
            raise ValueError(f"row {self.label!r}: bar {self.bar} out of range 0..100")


@dataclass(slots=True, frozen=True)
class Card:
    id: str
    icon: str
    title: str
    led_hint: int
    rows: list[Row] = field(default_factory=list)
    data_ts: int = 0  # epoch seconds of the source data, shown on L3 when show_timestamp=True
    show_timestamp: bool = True  # set False to reclaim L3 as a fifth content row


def fmt_countdown(seconds: int) -> str:
    """<=5-char countdown: NOW / 59m / 1h23m / 10h / 9d."""
    if seconds <= 0:
        return "NOW"
    minutes = seconds // 60
    if minutes < 60:
        return f"{minutes}m"
    hours = seconds // 3600
    rem_min = (seconds % 3600) // 60
    if hours < 10:
        return f"{hours}h{rem_min}m" if rem_min else f"{hours}h"
    if hours < 48:
        return f"{hours}h"
    return f"{seconds // 86400}d"


def _render_row(row: Row, *, now_ts: int) -> str:
    if row.text is not None:
        return row.text
    if row.bar is not None:
        return kp3.bar(row.bar)
    value = row.value if row.value is not None else fmt_countdown(row.until - now_ts)
    return kp3.kv(row.label, value)


def render_frame(card: Card, *, now: dt.datetime, generation: int, index: int, total: int) -> bytes:
    """Render a card to one KP3 frame, computing countdowns at render time and
    stamping the third line with the data timestamp when the card asks for it.

    Only the card's own shape is checked here; ``kp3.build_frame`` validates the
    frame it produces against the grammar the firmware enforces.
    """
    max_rows = MAX_ROWS - 1 if card.show_timestamp else MAX_ROWS
    if not 1 <= len(card.rows) <= max_rows:
        raise ValueError(f"card {card.id!r}: expected 1..{max_rows} rows, got {len(card.rows)}")

    now_ts = int(now.timestamp())
    rendered = [_render_row(r, now_ts=now_ts) for r in card.rows]
    rendered += [""] * (max_rows - len(rendered))
    if card.show_timestamp:
        data_time = dt.datetime.fromtimestamp(card.data_ts, tz=now.tzinfo).strftime("%H:%M")
        lines = [kp3.title(card.title), rendered[0], data_time, *rendered[1:]]
    else:
        lines = [kp3.title(card.title), *rendered]

    return kp3.build_frame(
        card.icon,
        *lines,
        generation=generation,
        index=index,
        total=total,
        led_hint=card.led_hint,
    ).encode("ascii")


def cache_dir() -> Path:
    return Path(os.environ.get(CACHE_ENV, CACHE_DEFAULT)).expanduser()


def write_card(card: Card) -> Path:
    """Validate + atomically publish one card (render dry-run fails fast at
    fetch time, so the pusher never sees an unrenderable card)."""
    render_frame(card, now=dt.datetime.now().astimezone(), generation=0, index=0, total=1)

    payload = {k: v for k, v in asdict(card).items() if v is not None}
    payload["rows"] = [{k: v for k, v in row.items() if v is not None} for row in payload["rows"]]

    directory = cache_dir()
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"{card.id}.json"
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(payload, ensure_ascii=False, separators=(",", ":")))
    tmp.replace(path)
    return path


def _order_key(card: Card) -> tuple[int, str]:
    try:
        return (CARD_ORDER.index(card.id), card.id)
    except ValueError:
        return (len(CARD_ORDER), card.id)


def load_cards() -> list[Card]:
    """Load every card in rotation order; malformed cache fails fast so stale
    producers or old frame schemas cannot silently keep the keyboard empty."""
    cards: list[Card] = []
    for path in cache_dir().glob("*.json"):
        try:
            data = json.loads(path.read_text())
            data["rows"] = [Row(**row) for row in data["rows"]]
            cards.append(Card(**data))
        except (json.JSONDecodeError, TypeError, ValueError, KeyError) as exc:
            raise ValueError(f"invalid KEYPOINT card cache file {path}: {exc}") from exc
    return sorted(cards, key=_order_key)


def fresh_cards(cards: list[Card], *, now: dt.datetime, max_age_s: int) -> list[Card]:
    """Drop cards whose source data is older than max_age_s — a dead fetcher
    falls out of the rotation instead of being shown as live."""
    cutoff = int(now.timestamp()) - max_age_s
    return [c for c in cards if c.data_ts >= cutoff]


def frames_to_push(*, now: dt.datetime, max_age_s: int) -> list[bytes]:
    """Load the cache, drop stale cards, and render the whole deck — each card
    stamped with its page index and the deck total (capped at PAGE_MAX). The
    firmware stores the deck; the keyboard's left keys page through it."""
    cards = fresh_cards(load_cards(), now=now, max_age_s=max_age_s)[: kp3.PAGE_MAX]
    total = len(cards)
    # GEN is a fixed-width hex field, so the deck transaction id wraps at its capacity.
    generation = int(now.timestamp()) % 16**kp3.GENERATION_FIELD_MAX
    return [render_frame(card, now=now, generation=generation, index=i, total=total) for i, card in enumerate(cards)]
