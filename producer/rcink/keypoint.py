"""KP3 live-card protocol + local card cache for the KEYPOINT keyboard display.

Wire contract mirrors zmk-config-KEYPOINT scripts/send_keypoint_live_demo.py
(the producer reference): ASCII frame
``KP3|GEN|IDX|TOTAL|ICON|LED|L1|..|L6``, six lines (0x20..0x7E, '|'
excluded, <=8 chars each), <=75 bytes total, written to a GATT characteristic
on the left keyboard half. GEN is a two-digit uppercase hex transaction id;
the firmware commits a deck only after every page for the same GEN arrives.
Screen convention: L1 title, L2-L6 content rows (L6 may be empty). LED is a
single digit trackpad status hint: 0 normal, 1 active, 2 attention, 3 warning,
4 error.

Bar row encoding: a line matching ``[NNN]`` (exactly 5 chars, N=decimal digit,
0-100) is rendered by the firmware as a filled progress rectangle rather than
text. Producers emit this via ``Row(bar=N)``; ``_render_row`` produces the token.

Fetchers drop one compact card per source into the cache dir (atomic replace,
one file per card — no cross-process locking needed); the keypoint-push job
reads the cache and renders frames at push time so countdowns (``Row.until``)
stay live between 15-minute fetches.
"""

from __future__ import annotations

import datetime as dt
import json
import os
from dataclasses import asdict, dataclass, field
from pathlib import Path

# Custom GATT service on the left keyboard half (encrypted write); see the
# KEYPOINT firmware widgets/live_data.{h,c} and send_keypoint_live_demo.py.
SERVICE_UUID = "f5d40000-6d2f-4f4b-9b2a-2f4a8e8c0001"
CHAR_UUID = "f5d40001-6d2f-4f4b-9b2a-2f4a8e8c0001"

PREFIX = "KP3"
GENERATION_FIELD_MAX = 2
TEXT_LINE_COUNT = 6
LINE_MAX = 8
MAX_ROWS = TEXT_LINE_COUNT - 1  # L1 is the title; L2..L6 are content.
PAGE_MAX = 8  # firmware deck capacity; idx/total are single decimal digits
FRAME_MAX_BYTES = 75  # KP3|GEN|IDX|TOTAL|ICON|LED|L1..L6
ICON_IDS = frozenset({"NONE", "SUN", "CLOUD", "RAIN", "TEMP", "WARN", "CODE", "TIME", "CODEX", "CLAUDE"})

LED_HINT_NONE = 0
LED_HINT_ACTIVE = 1
LED_HINT_ATTENTION = 2
LED_HINT_WARNING = 3
LED_HINT_ERROR = 4
LED_HINT_IDS = frozenset({LED_HINT_NONE, LED_HINT_ACTIVE, LED_HINT_ATTENTION, LED_HINT_WARNING, LED_HINT_ERROR})

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


def kv(label: str, value: str) -> str:
    """Pad label + value to a full LINE_MAX-wide line (monospace font turns the
    display into an 8x6 grid: labels left column, values right column)."""
    if len(label) + len(value) >= LINE_MAX:
        raise ValueError(f"kv({label!r}, {value!r}) does not fit {LINE_MAX} chars with a gap")
    return f"{label}{' ' * (LINE_MAX - len(label) - len(value))}{value}"


def title(text: str) -> str:
    """Left-anchor a card title by padding it to the full line width."""
    if len(text) > LINE_MAX:
        raise ValueError(f"title {text!r} longer than {LINE_MAX} chars")
    return text.ljust(LINE_MAX)


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


def _validate_line(line: str) -> str:
    if len(line) > LINE_MAX:
        raise ValueError(f"line {line!r} is {len(line)} chars, max {LINE_MAX}")
    for ch in line:
        if ch == "|" or not 0x20 <= ord(ch) <= 0x7E:
            raise ValueError(f"line {line!r} contains unsupported character {ch!r}")
    return line


def _validate_led_hint(led_hint: int) -> int:
    if isinstance(led_hint, bool) or led_hint not in LED_HINT_IDS:
        raise ValueError(f"unsupported LED hint {led_hint!r}; expected one of: {sorted(LED_HINT_IDS)}")
    return led_hint


def _validate_generation(generation: int) -> int:
    if isinstance(generation, bool) or not 0 <= generation <= 0xFF:
        raise ValueError(f"generation {generation!r} out of range 0..255")
    return generation


def _render_row(row: Row, *, now_ts: int) -> str:
    if row.text is not None:
        return row.text
    if row.bar is not None:
        return f"[{row.bar:03d}]"
    value = row.value if row.value is not None else fmt_countdown(row.until - now_ts)
    return kv(row.label, value)


def render_frame(card: Card, *, now: dt.datetime, generation: int, index: int, total: int) -> bytes:
    """Render a card to one full-screen KP3 frame: KP3|GEN|IDX|TOTAL|ICON|LED|L1..L6,
    computing countdowns at render time and stamping L3 with the data timestamp."""
    generation = _validate_generation(generation)
    if not 1 <= total <= PAGE_MAX:
        raise ValueError(f"total {total} out of range 1..{PAGE_MAX}")
    if not 0 <= index < total:
        raise ValueError(f"index {index} out of range for total {total}")
    if card.icon not in ICON_IDS:
        raise ValueError(f"unsupported icon {card.icon!r}; expected one of: {', '.join(sorted(ICON_IDS))}")
    led_hint = _validate_led_hint(card.led_hint)
    max_rows = MAX_ROWS if not card.show_timestamp else MAX_ROWS - 1
    if not 1 <= len(card.rows) <= max_rows:
        raise ValueError(f"card {card.id!r}: expected 1..{max_rows} rows, got {len(card.rows)}")

    now_ts = int(now.timestamp())
    rendered = [_render_row(r, now_ts=now_ts) for r in card.rows]
    rendered += [""] * (max_rows - len(rendered))
    if card.show_timestamp:
        data_time = dt.datetime.fromtimestamp(card.data_ts, tz=now.tzinfo).strftime("%H:%M")
        lines = [title(card.title), rendered[0], data_time, *rendered[1:]]
    else:
        lines = [title(card.title), *rendered]

    frame = "|".join(
        [
            PREFIX,
            f"{generation:02X}",
            str(index),
            str(total),
            card.icon,
            str(led_hint),
            *(_validate_line(line) for line in lines),
        ]
    ).encode("ascii")
    if len(frame) > FRAME_MAX_BYTES:
        raise ValueError(f"frame is {len(frame)} bytes, max {FRAME_MAX_BYTES}")
    return frame


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
    cards = fresh_cards(load_cards(), now=now, max_age_s=max_age_s)[:PAGE_MAX]
    total = len(cards)
    generation = int(now.timestamp()) & 0xFF
    return [render_frame(card, now=now, generation=generation, index=i, total=total) for i, card in enumerate(cards)]
