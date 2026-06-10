"""KP2 live-card protocol + local card cache for the KEYPOINT keyboard display.

Wire contract mirrors zmk-config-KEYPOINT scripts/send_keypoint_live_demo.py
(the producer reference): ASCII frame ``KP2|ICON|L1|..|L6``, six 8-char lines
(0x20..0x7E, '|' excluded), <=66 bytes, written to a GATT characteristic on the
left keyboard half. Screen convention: L1 title, L2 key metric, L3 data
timestamp (doubles as a clock), L4-L6 detail rows.

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

PREFIX = "KP2"
TEXT_LINE_COUNT = 6
LINE_MAX = 8
MAX_ROWS = 4  # rows[0] -> L2, rows[1:4] -> L4..L6
FRAME_MAX_BYTES = 66
ICON_IDS = frozenset({"NONE", "SUN", "CLOUD", "RAIN", "TEMP", "WARN", "CODE", "TIME", "CODEX", "CLAUDE"})

# Rotation order on the keyboard; unknown card ids sort after these, alphabetically.
CARD_ORDER = ("claude", "codex", "github", "calendar", "todo")

CACHE_ENV = "RCINK_KEYPOINT_CACHE"
CACHE_DEFAULT = "~/.cache/rcink/keypoint"


@dataclass(slots=True, frozen=True)
class Row:
    """One kv line. Either a literal ``value`` or an ``until`` epoch rendered
    as a live countdown at push time (so it does not freeze at fetch time)."""

    label: str
    value: str | None = None
    until: int | None = None

    def __post_init__(self) -> None:
        if (self.value is None) == (self.until is None):
            raise ValueError(f"row {self.label!r}: exactly one of value/until required")


@dataclass(slots=True, frozen=True)
class Card:
    id: str
    icon: str
    title: str
    rows: list[Row] = field(default_factory=list)
    data_ts: int = 0  # epoch seconds of the source data, shown on L3


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
    """<=4-char countdown: NOW / 99M / 47H / 9D."""
    if seconds <= 0:
        return "NOW"
    minutes = seconds // 60
    if minutes < 100:
        return f"{minutes}M"
    hours = seconds // 3600
    if hours < 48:
        return f"{hours}H"
    return f"{seconds // 86400}D"


def _validate_line(line: str) -> str:
    if len(line) > LINE_MAX:
        raise ValueError(f"line {line!r} is {len(line)} chars, max {LINE_MAX}")
    for ch in line:
        if ch == "|" or not 0x20 <= ord(ch) <= 0x7E:
            raise ValueError(f"line {line!r} contains unsupported character {ch!r}")
    return line


def _render_row(row: Row, *, now_ts: int) -> str:
    value = row.value if row.value is not None else fmt_countdown(row.until - now_ts)
    return kv(row.label, value)


def render_frame(card: Card, *, now: dt.datetime) -> bytes:
    """Render a card to one full-screen KP2 frame, computing countdowns at
    render time and stamping L3 with the card's data timestamp."""
    if card.icon not in ICON_IDS:
        raise ValueError(f"unsupported icon {card.icon!r}; expected one of: {', '.join(sorted(ICON_IDS))}")
    if not 1 <= len(card.rows) <= MAX_ROWS:
        raise ValueError(f"card {card.id!r}: expected 1..{MAX_ROWS} rows, got {len(card.rows)}")

    now_ts = int(now.timestamp())
    rendered = [_render_row(r, now_ts=now_ts) for r in card.rows]
    rendered += [""] * (MAX_ROWS - len(rendered))
    data_time = dt.datetime.fromtimestamp(card.data_ts, tz=now.tzinfo).strftime("%H:%M")
    lines = [title(card.title), rendered[0], data_time, *rendered[1:]]

    frame = "|".join([PREFIX, card.icon, *(_validate_line(line) for line in lines)]).encode("ascii")
    if len(frame) > FRAME_MAX_BYTES:
        raise ValueError(f"frame is {len(frame)} bytes, max {FRAME_MAX_BYTES}")
    return frame


def cache_dir() -> Path:
    return Path(os.environ.get(CACHE_ENV, CACHE_DEFAULT)).expanduser()


def write_card(card: Card) -> Path:
    """Validate + atomically publish one card (render dry-run fails fast at
    fetch time, so the pusher never sees an unrenderable card)."""
    render_frame(card, now=dt.datetime.now().astimezone())

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
    """Load every parseable card in rotation order; corrupt files are skipped
    (a half-broken cache should not take down the whole rotation)."""
    cards: list[Card] = []
    for path in cache_dir().glob("*.json"):
        try:
            data = json.loads(path.read_text())
            data["rows"] = [Row(**row) for row in data["rows"]]
            cards.append(Card(**data))
        except (json.JSONDecodeError, TypeError, ValueError, KeyError):
            continue
    return sorted(cards, key=_order_key)


def fresh_cards(cards: list[Card], *, now: dt.datetime, max_age_s: int) -> list[Card]:
    """Drop cards whose source data is older than max_age_s — a dead fetcher
    falls out of the rotation instead of being shown as live."""
    cutoff = int(now.timestamp()) - max_age_s
    return [c for c in cards if c.data_ts >= cutoff]


def frames_to_push(*, now: dt.datetime, max_age_s: int) -> list[bytes]:
    """Load the cache, drop stale cards, and render each survivor to a KP2
    frame in rotation order — everything the pusher needs except the BLE write."""
    cards = fresh_cards(load_cards(), now=now, max_age_s=max_age_s)
    return [render_frame(card, now=now) for card in cards]
