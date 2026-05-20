# tlsr-layout — Phase 1: Producer Skeleton

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Python producer with layout DSL builders, the zlib+CRC+chunker encode pipeline, a `dry-run` CLI that emits compressed payloads, and full unit-test coverage. No BLE, no firmware, no hardware in this phase.

**Architecture:** A single `tlsr_layout` Python package under `producer/`. Layout helpers mirror `rcpapercolor/producer/rcpaper/layout.py` exactly so producer-side pages can be reused. The encoder produces `(data_ver, compressed_bytes, chunks)` that match the wire format in the spec §6. CLI uses typer.

**Tech Stack:** Python ≥3.13, uv, typer, pytest, ruff, xxhash. Standard-lib `zlib`, `binascii.crc32`, `json`.

**Spec:** `docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`

**Phase boundary:** Phase 1 produces a working Python package with a CLI that can build a page, encode it, and dump JSON + compressed bytes + chunk frames to disk. No network or BLE. Phase 2 (separate plan) will fork tlsr-price-tag firmware. Phase 6 (separate plan) will wire `tlsr-layout serve` and `push` to bleak for real BLE.

---

## File Structure

```
producer/
  pyproject.toml                       # uv-managed
  .python-version                      # 3.13
  ruff.toml
  README.md
  src/tlsr_layout/
    __init__.py                        # package version
    palette.py                         # color name set + validators
    layout.py                          # vstack/hstack/text/... helpers
    encode.py                          # json + zlib + crc32 + chunker
    hash.py                            # xxhash32 wrapper for data_ver
    protocol.py                        # BEGIN/COMMIT frame builders (no I/O)
    pages/__init__.py
    pages/example.py                   # one demo page builder
    cli.py                             # typer; dry-run only in Phase 1
  tests/
    __init__.py
    test_palette.py
    test_layout.py
    test_layout_snapshot.py
    golden/
      example_page.json                # snapshot
    test_encode.py
    test_hash.py
    test_protocol.py
    test_cli_dry_run.py
```

Each file has one responsibility:
- `palette.py` — palette names + validation; nothing else.
- `layout.py` — pure dict builders; no I/O, no encoding.
- `encode.py` — JSON serialisation + zlib + CRC + chunker.
- `hash.py` — xxhash32 for `data_ver`.
- `protocol.py` — bytes-level frame layouts from spec §6.1/6.2 (no transport).
- `cli.py` — typer surface; `dry-run` is the only working command.

---

## Task 1: Project scaffold

**Files:**
- Create: `producer/pyproject.toml`
- Create: `producer/.python-version`
- Create: `producer/ruff.toml`
- Create: `producer/README.md`
- Create: `producer/src/tlsr_layout/__init__.py`
- Create: `producer/tests/__init__.py`

- [ ] **Step 1: Create the project root**

```bash
mkdir -p producer/src/tlsr_layout/pages producer/tests/golden
```

- [ ] **Step 2: Write `producer/pyproject.toml`**

```toml
[project]
name = "tlsr-layout"
version = "0.1.0"
description = "Mac BLE producer that streams JSON layout DSL to TLSR8258 e-paper tags."
requires-python = ">=3.13"
dependencies = [
  "typer>=0.12",
  "xxhash>=3.4",
]

[project.scripts]
tlsr-layout = "tlsr_layout.cli:app"

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.hatch.build.targets.wheel]
packages = ["src/tlsr_layout"]

[dependency-groups]
dev = [
  "pytest>=8",
  "pytest-cov>=5",
  "ruff>=0.6",
]
```

- [ ] **Step 3: Write `producer/.python-version`**

```
3.13
```

- [ ] **Step 4: Write `producer/ruff.toml`**

```toml
line-length = 100
target-version = "py313"

[lint]
select = ["E", "F", "I", "UP", "B", "SIM"]
```

- [ ] **Step 5: Write `producer/README.md`**

```markdown
# tlsr-layout producer

Mac-side BLE producer for the TLSR8258 layout-DSL e-paper tag.

Phase 1: dry-run only — build pages, encode, dump to disk. No BLE yet.

## Dev

```bash
cd producer
uv sync --all-extras
uv run pytest
uv run tlsr-layout --help
```

See `docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md` for the wire protocol.
```

- [ ] **Step 6: Write `producer/src/tlsr_layout/__init__.py`**

```python
"""Mac BLE producer for the TLSR8258 layout-DSL e-paper tag."""

__version__ = "0.1.0"
```

- [ ] **Step 7: Write `producer/tests/__init__.py` (empty marker)**

```python
```

- [ ] **Step 8: Sync deps and run the empty test suite**

Run: `cd producer && uv sync --all-extras`
Expected: lockfile created, no errors.

Run: `cd producer && uv run pytest`
Expected: `no tests ran` (collected 0 items). Exit 0 (or 5 — both fine, we only care no errors).

- [ ] **Step 9: Commit**

```bash
git add producer/
git commit -m "feat(producer): scaffold tlsr-layout python package"
```

---

## Task 2: Color palette

**Files:**
- Create: `producer/src/tlsr_layout/palette.py`
- Create: `producer/tests/test_palette.py`

The producer uses the rcpapercolor 6-color vocabulary; the firmware maps unsupported colors to {black, white, red} per spec §7.3.

- [ ] **Step 1: Write the failing test `producer/tests/test_palette.py`**

```python
import pytest

from tlsr_layout.palette import COLORS, is_valid_color, validate_color


def test_six_colors_defined():
    assert COLORS == {"black", "white", "red", "yellow", "green", "blue"}


def test_is_valid_color_true():
    for c in ("black", "white", "red", "yellow", "green", "blue"):
        assert is_valid_color(c) is True


def test_is_valid_color_false():
    assert is_valid_color("purple") is False
    assert is_valid_color("BLACK") is False  # case-sensitive
    assert is_valid_color("") is False
    assert is_valid_color(None) is False  # type: ignore[arg-type]


def test_validate_color_passes_through():
    assert validate_color("red") == "red"


def test_validate_color_raises_on_unknown():
    with pytest.raises(ValueError, match="unknown color 'purple'"):
        validate_color("purple")


def test_validate_color_allows_none_when_optional():
    assert validate_color(None, allow_none=True) is None


def test_validate_color_rejects_none_when_required():
    with pytest.raises(ValueError, match="color is required"):
        validate_color(None)
```

- [ ] **Step 2: Run test to verify failure**

Run: `cd producer && uv run pytest tests/test_palette.py -v`
Expected: collection error / ImportError on `tlsr_layout.palette`.

- [ ] **Step 3: Implement `producer/src/tlsr_layout/palette.py`**

```python
"""Six-color palette used by rcpapercolor; firmware maps unsupported names."""

COLORS: frozenset[str] = frozenset(
    {"black", "white", "red", "yellow", "green", "blue"}
)


def is_valid_color(name: object) -> bool:
    return isinstance(name, str) and name in COLORS


def validate_color(name: str | None, *, allow_none: bool = False) -> str | None:
    if name is None:
        if allow_none:
            return None
        raise ValueError("color is required")
    if name not in COLORS:
        raise ValueError(f"unknown color {name!r}; expected one of {sorted(COLORS)}")
    return name
```

- [ ] **Step 4: Run tests to verify pass**

Run: `cd producer && uv run pytest tests/test_palette.py -v`
Expected: 6 passed.

- [ ] **Step 5: Commit**

```bash
git add producer/src/tlsr_layout/palette.py producer/tests/test_palette.py
git commit -m "feat(producer): color palette + validators"
```

---

## Task 3: Layout primitives

**Files:**
- Create: `producer/src/tlsr_layout/layout.py`
- Create: `producer/tests/test_layout.py`

Port `rcpapercolor/producer/rcpaper/layout.py` verbatim, then add palette validation on the `color` / `bg` arguments.

- [ ] **Step 1: Write the failing test `producer/tests/test_layout.py`**

```python
import pytest

from tlsr_layout.layout import (
    divider,
    hbar,
    hstack,
    icon,
    image,
    marker,
    sparkline,
    spacer,
    text,
    vstack,
)


def test_vstack_drops_none_values():
    node = vstack()
    assert node == {"type": "vstack", "children": []}


def test_vstack_keeps_explicit_zero():
    node = vstack(gap=0, pad=0)
    assert node["gap"] == 0
    assert node["pad"] == 0


def test_vstack_keeps_children():
    leaf = text("hi", color="black")
    node = vstack(children=[leaf])
    assert node["children"] == [leaf]


def test_hstack_shape():
    node = hstack(gap=4, pad=2, align="center", children=[])
    assert node == {
        "type": "hstack",
        "gap": 4,
        "pad": 2,
        "align": "center",
        "children": [],
    }


def test_text_minimal():
    assert text("hello") == {"type": "text", "value": "hello"}


def test_text_full():
    node = text(
        "X",
        size="h1",
        color="white",
        weight="bold",
        align="center",
        bg="red",
        pad=8,
    )
    assert node == {
        "type": "text",
        "value": "X",
        "size": "h1",
        "color": "white",
        "weight": "bold",
        "align": "center",
        "bg": "red",
        "pad": 8,
    }


def test_text_rejects_unknown_color():
    with pytest.raises(ValueError, match="unknown color 'purple'"):
        text("X", color="purple")


def test_text_rejects_unknown_bg():
    with pytest.raises(ValueError, match="unknown color 'mauve'"):
        text("X", bg="mauve")


def test_spacer_no_size():
    assert spacer() == {"type": "spacer"}


def test_spacer_with_size():
    assert spacer(size=12) == {"type": "spacer", "size": 12}


def test_divider_validates_color():
    with pytest.raises(ValueError):
        divider(color="not-a-color")
    assert divider(color="black", thickness=2) == {
        "type": "divider",
        "color": "black",
        "thickness": 2,
    }


def test_hbar_minimal_requires_pct():
    node = hbar(pct=42)
    assert node == {"type": "hbar", "pct": 42}


def test_hbar_full():
    node = hbar(
        pct=42,
        color="blue",
        height=14,
        label_left="A",
        label_right="B",
    )
    assert node["color"] == "blue"
    assert node["label_left"] == "A"


def test_icon():
    assert icon("cpu", size=24, color="red") == {
        "type": "icon",
        "name": "cpu",
        "size": 24,
        "color": "red",
    }


def test_image_requires_exactly_one_of_name_or_url():
    with pytest.raises(ValueError, match="exactly one"):
        image()
    with pytest.raises(ValueError, match="exactly one"):
        image(name="x", url="http://y")
    assert image(name="x")["name"] == "x"
    assert image(url="http://y")["url"] == "http://y"


def test_sparkline_copies_values():
    src = [1, 2, 3]
    node = sparkline(src, color="blue", height=20, baseline="auto")
    assert node["values"] == [1, 2, 3]
    src.append(4)
    assert node["values"] == [1, 2, 3]  # producer copy is independent


def test_marker():
    assert marker(pct=50, color="red") == {"type": "marker", "pct": 50, "color": "red"}
```

- [ ] **Step 2: Run test to verify failure**

Run: `cd producer && uv run pytest tests/test_layout.py -v`
Expected: ImportError on `tlsr_layout.layout`.

- [ ] **Step 3: Implement `producer/src/tlsr_layout/layout.py`**

```python
"""Tiny layout DSL — dict-builders matching the on-the-wire JSON schema.

Mirrors rcpapercolor/producer/rcpaper/layout.py; adds palette validation.
"""

from typing import Any

from .palette import validate_color


def _node(t: str, **props: Any) -> dict[str, Any]:
    out: dict[str, Any] = {"type": t}
    for k, v in props.items():
        if v is not None:
            out[k] = v
    return out


def vstack(
    *,
    gap: int | None = None,
    pad: int | None = None,
    align: str | None = None,
    children: list[dict] | None = None,
) -> dict:
    return _node(
        "vstack",
        gap=gap,
        pad=pad,
        align=align,
        children=children if children is not None else [],
    )


def hstack(
    *,
    gap: int | None = None,
    pad: int | None = None,
    align: str | None = None,
    children: list[dict] | None = None,
) -> dict:
    return _node(
        "hstack",
        gap=gap,
        pad=pad,
        align=align,
        children=children if children is not None else [],
    )


def spacer(*, size: int | None = None) -> dict:
    return _node("spacer", size=size)


def divider(*, color: str | None = None, thickness: int | None = None) -> dict:
    return _node(
        "divider",
        color=validate_color(color, allow_none=True),
        thickness=thickness,
    )


def text(
    value: str,
    *,
    size: str | None = None,
    color: str | None = None,
    weight: str | None = None,
    align: str | None = None,
    bg: str | None = None,
    pad: int | None = None,
) -> dict:
    return _node(
        "text",
        value=value,
        size=size,
        color=validate_color(color, allow_none=True),
        weight=weight,
        align=align,
        bg=validate_color(bg, allow_none=True),
        pad=pad,
    )


def hbar(
    *,
    pct: float,
    color: str | None = None,
    height: int | None = None,
    label_left: str | None = None,
    label_right: str | None = None,
) -> dict:
    return _node(
        "hbar",
        pct=pct,
        color=validate_color(color, allow_none=True),
        height=height,
        label_left=label_left,
        label_right=label_right,
    )


def icon(name: str, *, size: int | None = None, color: str | None = None) -> dict:
    return _node(
        "icon",
        name=name,
        size=size,
        color=validate_color(color, allow_none=True),
    )


def image(
    name: str | None = None,
    *,
    url: str | None = None,
    format: str | None = None,
    width: int | None = None,
    height: int | None = None,
) -> dict:
    if bool(name) == bool(url):
        raise ValueError("image() requires exactly one of name=... or url=...")
    return _node(
        "image",
        name=name,
        url=url,
        format=format,
        width=width,
        height=height,
    )


def sparkline(
    values: list[float],
    *,
    color: str | None = None,
    height: int | None = None,
    baseline: str | int | None = None,
) -> dict:
    return _node(
        "sparkline",
        values=list(values),
        color=validate_color(color, allow_none=True),
        height=height,
        baseline=baseline,
    )


def marker(*, pct: float, color: str | None = None) -> dict:
    return _node(
        "marker",
        pct=pct,
        color=validate_color(color, allow_none=True),
    )
```

- [ ] **Step 4: Run tests to verify pass**

Run: `cd producer && uv run pytest tests/test_layout.py -v`
Expected: all 16 tests pass.

- [ ] **Step 5: Commit**

```bash
git add producer/src/tlsr_layout/layout.py producer/tests/test_layout.py
git commit -m "feat(producer): layout DSL primitives with palette validation"
```

---

## Task 4: Example page + JSON snapshot golden

**Files:**
- Create: `producer/src/tlsr_layout/pages/__init__.py`
- Create: `producer/src/tlsr_layout/pages/example.py`
- Create: `producer/tests/golden/example_page.json`
- Create: `producer/tests/test_layout_snapshot.py`

- [ ] **Step 1: Write `producer/src/tlsr_layout/pages/__init__.py` (empty)**

```python
```

- [ ] **Step 2: Write `producer/src/tlsr_layout/pages/example.py`**

```python
"""Demo page used in tests and the dry-run CLI."""

from ..layout import hbar, sparkline, spacer, text, vstack


def example_page() -> dict:
    """A small dashboard page that exercises all common primitives."""

    return vstack(
        pad=14,
        gap=6,
        children=[
            text(
                "CLAUDE",
                size="h1",
                color="white",
                bg="red",
                pad=8,
                align="center",
                weight="bold",
            ),
            hbar(pct=42, color="blue", height=14),
            sparkline([12, 18, 14, 22, 19, 28, 31], color="blue", height=40),
            spacer(),
            text("updated 14:32", size="sm", align="center"),
        ],
    )
```

- [ ] **Step 3: Write `producer/tests/golden/example_page.json` (exact bytes — generate then commit)**

```json
{
  "type": "vstack",
  "gap": 6,
  "pad": 14,
  "children": [
    {
      "type": "text",
      "value": "CLAUDE",
      "size": "h1",
      "color": "white",
      "weight": "bold",
      "align": "center",
      "bg": "red",
      "pad": 8
    },
    {
      "type": "hbar",
      "pct": 42,
      "color": "blue",
      "height": 14
    },
    {
      "type": "sparkline",
      "values": [12, 18, 14, 22, 19, 28, 31],
      "color": "blue",
      "height": 40
    },
    {
      "type": "spacer"
    },
    {
      "type": "text",
      "value": "updated 14:32",
      "size": "sm",
      "align": "center"
    }
  ]
}
```

- [ ] **Step 4: Write `producer/tests/test_layout_snapshot.py`**

```python
import json
from pathlib import Path

from tlsr_layout.pages.example import example_page

GOLDEN = Path(__file__).parent / "golden" / "example_page.json"


def test_example_page_matches_golden():
    actual = example_page()
    expected = json.loads(GOLDEN.read_text())
    assert actual == expected
```

- [ ] **Step 5: Run test to verify pass**

Run: `cd producer && uv run pytest tests/test_layout_snapshot.py -v`
Expected: 1 passed.

If it fails because key-ordering differs, **do not rewrite the test** — the test compares parsed dicts (insertion order doesn't matter for equality). If a value mismatches, fix the page builder (golden is the source of truth).

- [ ] **Step 6: Commit**

```bash
git add producer/src/tlsr_layout/pages/ producer/tests/golden/ producer/tests/test_layout_snapshot.py
git commit -m "feat(producer): example page + golden snapshot test"
```

---

## Task 5: data_ver hash (xxhash32)

**Files:**
- Create: `producer/src/tlsr_layout/hash.py`
- Create: `producer/tests/test_hash.py`

The firmware computes xxhash32 over the **uncompressed** JSON bytes. Producer must agree on the bytes (`json.dumps(..., ensure_ascii=False, separators=(",", ":"))` — compact, no whitespace, no `\u`-escaped non-ASCII).

- [ ] **Step 1: Write the failing test `producer/tests/test_hash.py`**

```python
from tlsr_layout.hash import canonical_json, data_ver


def test_canonical_json_compact():
    assert canonical_json({"a": 1, "b": [2, 3]}) == b'{"a":1,"b":[2,3]}'


def test_canonical_json_keeps_utf8_literal():
    # ensure_ascii=False: Chinese stays as UTF-8 bytes, not \uXXXX.
    blob = canonical_json({"value": "你好"})
    assert "你好".encode() in blob
    assert b"\\u" not in blob


def test_data_ver_stable_across_runs():
    page = {"type": "text", "value": "hi"}
    a = data_ver(page)
    b = data_ver(page)
    assert a == b
    assert isinstance(a, int)
    assert 0 <= a <= 0xFFFFFFFF


def test_data_ver_changes_on_content_change():
    a = data_ver({"type": "text", "value": "hi"})
    b = data_ver({"type": "text", "value": "ho"})
    assert a != b


def test_data_ver_insensitive_to_key_order():
    # Canonical encoding sorts keys.
    a = data_ver({"a": 1, "b": 2})
    b = data_ver({"b": 2, "a": 1})
    assert a == b
```

- [ ] **Step 2: Run test to verify failure**

Run: `cd producer && uv run pytest tests/test_hash.py -v`
Expected: ImportError.

- [ ] **Step 3: Implement `producer/src/tlsr_layout/hash.py`**

```python
"""Canonical JSON encoding + xxhash32 data_ver."""

import json
from typing import Any

import xxhash


def canonical_json(node: Any) -> bytes:
    """Compact, sorted-key, ensure_ascii=False JSON bytes.

    Sorted keys means the same logical page always produces the same bytes
    regardless of how the builder dropped properties. ensure_ascii=False
    keeps non-ASCII characters as UTF-8 (smaller payloads after zlib).
    """

    return json.dumps(
        node,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def data_ver(node: Any) -> int:
    """32-bit xxhash of the canonical JSON bytes.

    Matches the value the firmware advertises in `current_data_ver`.
    """

    return xxhash.xxh32(canonical_json(node)).intdigest()
```

- [ ] **Step 4: Run tests to verify pass**

Run: `cd producer && uv run pytest tests/test_hash.py -v`
Expected: 5 passed.

- [ ] **Step 5: Commit**

```bash
git add producer/src/tlsr_layout/hash.py producer/tests/test_hash.py
git commit -m "feat(producer): canonical JSON + xxhash32 data_ver"
```

---

## Task 6: Encode pipeline (zlib + CRC + chunker)

**Files:**
- Create: `producer/src/tlsr_layout/encode.py`
- Create: `producer/tests/test_encode.py`

Produces the byte stream and chunk frames the firmware will receive (spec §6).

- [ ] **Step 1: Write the failing test `producer/tests/test_encode.py`**

```python
import zlib

import pytest

from tlsr_layout.encode import EncodedPage, chunk_payload, encode_page


def test_encode_page_roundtrip():
    page = {"type": "text", "value": "hi"}
    encoded = encode_page(page)

    assert isinstance(encoded, EncodedPage)
    assert encoded.uncompressed_len == len(encoded.canonical_json)
    assert encoded.compressed_len == len(encoded.compressed)
    assert encoded.crc32 == zlib.crc32(encoded.compressed) & 0xFFFFFFFF
    assert zlib.decompress(encoded.compressed) == encoded.canonical_json
    assert 0 <= encoded.data_ver <= 0xFFFFFFFF


def test_encode_page_compact_json():
    page = {"a": 1, "b": [2, 3]}
    encoded = encode_page(page)
    assert encoded.canonical_json == b'{"a":1,"b":[2,3]}'


def test_chunk_payload_exact_division():
    payload = b"A" * 360
    chunks = chunk_payload(payload, max_payload=180)
    assert len(chunks) == 2
    assert chunks[0] == (0, b"A" * 180)
    assert chunks[1] == (1, b"A" * 180)


def test_chunk_payload_with_remainder():
    payload = b"X" * 181
    chunks = chunk_payload(payload, max_payload=180)
    assert len(chunks) == 2
    assert chunks[0] == (0, b"X" * 180)
    assert chunks[1] == (1, b"X")


def test_chunk_payload_single_chunk():
    chunks = chunk_payload(b"hello", max_payload=180)
    assert chunks == [(0, b"hello")]


def test_chunk_payload_empty():
    chunks = chunk_payload(b"", max_payload=180)
    assert chunks == []


def test_chunk_payload_caps_at_max_chunks():
    payload = b"A" * (256 * 180 + 1)  # would need 257 chunks
    with pytest.raises(ValueError, match="too many chunks"):
        chunk_payload(payload, max_payload=180)


def test_chunk_payload_rejects_bad_max_payload():
    with pytest.raises(ValueError, match="max_payload"):
        chunk_payload(b"x", max_payload=0)
    with pytest.raises(ValueError, match="max_payload"):
        chunk_payload(b"x", max_payload=-1)
```

- [ ] **Step 2: Run test to verify failure**

Run: `cd producer && uv run pytest tests/test_encode.py -v`
Expected: ImportError.

- [ ] **Step 3: Implement `producer/src/tlsr_layout/encode.py`**

```python
"""Layout DSL → wire bytes.

Pipeline:
    page (dict)
        → canonical_json (UTF-8 bytes)
        → zlib.compress (bytes)
        → chunk_payload (list of (seq, payload))

Matches spec §6.1 (BEGIN frame fields) and §6.2 (XFER_DATA chunks).
"""

import zlib
from dataclasses import dataclass
from typing import Any

from .hash import canonical_json, data_ver

MAX_CHUNKS = 256  # spec §6.1 cap


@dataclass(slots=True, frozen=True)
class EncodedPage:
    canonical_json: bytes
    compressed: bytes
    data_ver: int
    crc32: int

    @property
    def uncompressed_len(self) -> int:
        return len(self.canonical_json)

    @property
    def compressed_len(self) -> int:
        return len(self.compressed)


def encode_page(page: Any) -> EncodedPage:
    raw = canonical_json(page)
    compressed = zlib.compress(raw, level=9)
    return EncodedPage(
        canonical_json=raw,
        compressed=compressed,
        data_ver=data_ver(page),
        crc32=zlib.crc32(compressed) & 0xFFFFFFFF,
    )


def chunk_payload(payload: bytes, *, max_payload: int) -> list[tuple[int, bytes]]:
    """Split payload into (seq, slice) tuples each ≤ max_payload bytes."""

    if max_payload <= 0:
        raise ValueError(f"max_payload must be > 0, got {max_payload}")
    if not payload:
        return []
    out: list[tuple[int, bytes]] = []
    for seq, off in enumerate(range(0, len(payload), max_payload)):
        if seq >= MAX_CHUNKS:
            raise ValueError(
                f"too many chunks: payload {len(payload)} B / {max_payload} B exceeds {MAX_CHUNKS}"
            )
        out.append((seq, payload[off : off + max_payload]))
    return out
```

- [ ] **Step 4: Run tests to verify pass**

Run: `cd producer && uv run pytest tests/test_encode.py -v`
Expected: 8 passed.

- [ ] **Step 5: Commit**

```bash
git add producer/src/tlsr_layout/encode.py producer/tests/test_encode.py
git commit -m "feat(producer): zlib+CRC encoder and chunker"
```

---

## Task 7: Protocol frame builders

**Files:**
- Create: `producer/src/tlsr_layout/protocol.py`
- Create: `producer/tests/test_protocol.py`

Pure byte-layout helpers per spec §6.1. No transport — just `bytes` in, `bytes` out. The Phase 6 BLE central will use these.

- [ ] **Step 1: Write the failing test `producer/tests/test_protocol.py`**

```python
import struct

import pytest

from tlsr_layout.protocol import (
    DSL_FORMAT_ZLIB_JSON_V1,
    XFER_ABORT,
    XFER_BEGIN,
    XFER_COMMIT,
    build_abort,
    build_begin,
    build_commit,
    build_data_chunk,
    parse_ack,
)


def test_build_begin_layout():
    frame = build_begin(
        data_ver=0xDEADBEEF,
        total_len=1024,
        total_chunks=6,
        crc32=0x12345678,
        dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
    )
    assert len(frame) == 20
    assert frame[0] == XFER_BEGIN
    # Little-endian per spec
    assert struct.unpack_from("<I", frame, 1)[0] == 0xDEADBEEF
    assert struct.unpack_from("<H", frame, 5)[0] == 1024
    assert struct.unpack_from("<H", frame, 7)[0] == 6
    assert struct.unpack_from("<I", frame, 9)[0] == 0x12345678
    assert frame[13] == DSL_FORMAT_ZLIB_JSON_V1
    assert frame[14:20] == b"\x00" * 6


def test_build_abort():
    frame = build_abort()
    assert len(frame) == 20
    assert frame[0] == XFER_ABORT
    assert frame[1:] == b"\x00" * 19


def test_build_commit():
    frame = build_commit(data_ver=0xCAFEBABE)
    assert len(frame) == 20
    assert frame[0] == XFER_COMMIT
    assert struct.unpack_from("<I", frame, 1)[0] == 0xCAFEBABE
    assert frame[5:] == b"\x00" * 15


def test_build_data_chunk():
    frame = build_data_chunk(seq=7, payload=b"hello")
    assert frame == struct.pack("<H", 7) + b"hello"


def test_build_data_chunk_rejects_oversize_seq():
    with pytest.raises(ValueError):
        build_data_chunk(seq=-1, payload=b"x")
    with pytest.raises(ValueError):
        build_data_chunk(seq=0x10000, payload=b"x")


def test_build_begin_rejects_out_of_range():
    with pytest.raises(ValueError, match="total_len"):
        build_begin(
            data_ver=0,
            total_len=0x10000,  # >u16
            total_chunks=1,
            crc32=0,
            dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
        )
    with pytest.raises(ValueError, match="total_chunks"):
        build_begin(
            data_ver=0,
            total_len=1,
            total_chunks=0x10000,
            crc32=0,
            dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
        )


def test_parse_ack_progress():
    raw = struct.pack("<HBB", 42, 0x00, 0x00)
    ack = parse_ack(raw)
    assert ack.last_seq == 42
    assert ack.code == 0x00
    assert ack.flags == 0
    assert ack.is_ok is True
    assert ack.is_error is False


def test_parse_ack_error():
    raw = struct.pack("<HBB", 5, 0x10, 0x00)  # ERR_GAP
    ack = parse_ack(raw)
    assert ack.code == 0x10
    assert ack.is_ok is False
    assert ack.is_error is True


def test_parse_ack_rejects_wrong_length():
    with pytest.raises(ValueError, match="ack must be 4 bytes"):
        parse_ack(b"\x00" * 3)
    with pytest.raises(ValueError, match="ack must be 4 bytes"):
        parse_ack(b"\x00" * 5)
```

- [ ] **Step 2: Run test to verify failure**

Run: `cd producer && uv run pytest tests/test_protocol.py -v`
Expected: ImportError.

- [ ] **Step 3: Implement `producer/src/tlsr_layout/protocol.py`**

```python
"""Wire-format frame builders for spec §6.

All multi-byte fields are little-endian. Reserved fields are zero-filled.
"""

import struct
from dataclasses import dataclass

XFER_BEGIN = 0x01
XFER_ABORT = 0x02
XFER_COMMIT = 0x03

DSL_FORMAT_ZLIB_JSON_V1 = 0x01

ACK_OK_PROGRESS = 0x00
ACK_OK_BEGIN = 0x01
ACK_OK_COMMIT = 0x02
ACK_ERR_GAP = 0x10
ACK_ERR_CRC = 0x11
ACK_ERR_INFLATE = 0x12
ACK_ERR_PARSE = 0x13
ACK_ERR_RENDER = 0x14
ACK_ERR_LOW_BATT = 0x20
ACK_ERR_BUSY = 0x21


def _check_u16(name: str, value: int) -> None:
    if not 0 <= value <= 0xFFFF:
        raise ValueError(f"{name} out of u16 range: {value}")


def _check_u32(name: str, value: int) -> None:
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError(f"{name} out of u32 range: {value}")


def build_begin(
    *,
    data_ver: int,
    total_len: int,
    total_chunks: int,
    crc32: int,
    dsl_format: int,
) -> bytes:
    _check_u32("data_ver", data_ver)
    _check_u16("total_len", total_len)
    _check_u16("total_chunks", total_chunks)
    _check_u32("crc32", crc32)
    if not 0 <= dsl_format <= 0xFF:
        raise ValueError(f"dsl_format out of u8 range: {dsl_format}")
    return (
        bytes([XFER_BEGIN])
        + struct.pack("<IHHIB", data_ver, total_len, total_chunks, crc32, dsl_format)
        + b"\x00" * 6
    )


def build_abort() -> bytes:
    return bytes([XFER_ABORT]) + b"\x00" * 19


def build_commit(*, data_ver: int) -> bytes:
    _check_u32("data_ver", data_ver)
    return bytes([XFER_COMMIT]) + struct.pack("<I", data_ver) + b"\x00" * 15


def build_data_chunk(*, seq: int, payload: bytes) -> bytes:
    _check_u16("seq", seq)
    return struct.pack("<H", seq) + payload


@dataclass(slots=True, frozen=True)
class Ack:
    last_seq: int
    code: int
    flags: int

    @property
    def is_ok(self) -> bool:
        return self.code < 0x10

    @property
    def is_error(self) -> bool:
        return self.code >= 0x10


def parse_ack(raw: bytes) -> Ack:
    if len(raw) != 4:
        raise ValueError(f"ack must be 4 bytes, got {len(raw)}")
    last_seq, code, flags = struct.unpack("<HBB", raw)
    return Ack(last_seq=last_seq, code=code, flags=flags)
```

- [ ] **Step 4: Run tests to verify pass**

Run: `cd producer && uv run pytest tests/test_protocol.py -v`
Expected: 9 passed.

- [ ] **Step 5: Commit**

```bash
git add producer/src/tlsr_layout/protocol.py producer/tests/test_protocol.py
git commit -m "feat(producer): wire-format frame builders + ack parser"
```

---

## Task 8: CLI scaffold with working `dry-run`

**Files:**
- Create: `producer/src/tlsr_layout/cli.py`
- Create: `producer/tests/test_cli_dry_run.py`

`dry-run` builds the example page, encodes it, writes JSON + compressed bytes + chunk frames to an output dir. `devices`, `push`, `serve` are stubs that print "not implemented in Phase 1".

- [ ] **Step 1: Write the failing test `producer/tests/test_cli_dry_run.py`**

```python
import json
import zlib
from pathlib import Path

from typer.testing import CliRunner

from tlsr_layout.cli import app

runner = CliRunner()


def test_dry_run_writes_artifacts(tmp_path: Path):
    out = tmp_path / "out"
    result = runner.invoke(app, ["dry-run", "example", "--out", str(out)])
    assert result.exit_code == 0, result.output

    canonical = out / "example.json"
    compressed = out / "example.zlib"
    chunks = out / "example.chunks.bin"
    summary = out / "example.summary.json"

    for p in (canonical, compressed, chunks, summary):
        assert p.exists(), f"expected {p}"

    parsed = json.loads(canonical.read_text())
    assert parsed["type"] == "vstack"

    raw = canonical.read_bytes()
    assert zlib.decompress(compressed.read_bytes()) == raw

    info = json.loads(summary.read_text())
    assert info["page"] == "example"
    assert info["uncompressed_len"] == len(raw)
    assert info["compressed_len"] == len(compressed.read_bytes())
    assert info["chunks"] >= 1
    assert "data_ver" in info
    assert "crc32" in info


def test_dry_run_unknown_page(tmp_path: Path):
    result = runner.invoke(app, ["dry-run", "nope", "--out", str(tmp_path)])
    assert result.exit_code != 0
    assert "unknown page" in result.output.lower()


def test_devices_stub():
    result = runner.invoke(app, ["devices"])
    assert result.exit_code == 0
    assert "not implemented" in result.output.lower()


def test_push_stub():
    result = runner.invoke(app, ["push", "example"])
    assert result.exit_code == 0
    assert "not implemented" in result.output.lower()


def test_serve_stub():
    result = runner.invoke(app, ["serve"])
    assert result.exit_code == 0
    assert "not implemented" in result.output.lower()
```

- [ ] **Step 2: Run test to verify failure**

Run: `cd producer && uv run pytest tests/test_cli_dry_run.py -v`
Expected: ImportError on `tlsr_layout.cli`.

- [ ] **Step 3: Implement `producer/src/tlsr_layout/cli.py`**

```python
"""Phase 1 CLI: dry-run is the only working command."""

import json
import struct
from pathlib import Path
from typing import Callable

import typer

from .encode import chunk_payload, encode_page
from .pages.example import example_page
from .protocol import DSL_FORMAT_ZLIB_JSON_V1, build_begin, build_data_chunk

app = typer.Typer(no_args_is_help=True, add_completion=False)

PAGES: dict[str, Callable[[], dict]] = {
    "example": example_page,
}

# Phase 1 uses the conservative MTU; firmware will negotiate up later.
DEFAULT_CHUNK_PAYLOAD = 180


@app.command("dry-run")
def dry_run(
    page_name: str = typer.Argument(..., help="Registered page name (see `tlsr-layout devices`)."),
    out: Path = typer.Option(Path("./out"), "--out", help="Output directory."),
    chunk_payload_size: int = typer.Option(
        DEFAULT_CHUNK_PAYLOAD, "--chunk", help="Bytes per XFER_DATA chunk."
    ),
) -> None:
    """Build a page, encode it, write JSON + compressed bytes + chunk frames to disk."""

    builder = PAGES.get(page_name)
    if builder is None:
        typer.echo(f"unknown page {page_name!r}; known: {sorted(PAGES)}", err=True)
        raise typer.Exit(code=2)

    out.mkdir(parents=True, exist_ok=True)
    page = builder()
    encoded = encode_page(page)

    (out / f"{page_name}.json").write_bytes(encoded.canonical_json)
    (out / f"{page_name}.zlib").write_bytes(encoded.compressed)

    chunks = chunk_payload(encoded.compressed, max_payload=chunk_payload_size)

    with (out / f"{page_name}.chunks.bin").open("wb") as fh:
        # BEGIN frame, then each XFER_DATA chunk in order, length-prefixed for offline replay.
        begin = build_begin(
            data_ver=encoded.data_ver,
            total_len=encoded.compressed_len,
            total_chunks=len(chunks),
            crc32=encoded.crc32,
            dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
        )
        fh.write(struct.pack("<H", len(begin)))
        fh.write(begin)
        for seq, payload in chunks:
            frame = build_data_chunk(seq=seq, payload=payload)
            fh.write(struct.pack("<H", len(frame)))
            fh.write(frame)

    summary = {
        "page": page_name,
        "data_ver": encoded.data_ver,
        "uncompressed_len": encoded.uncompressed_len,
        "compressed_len": encoded.compressed_len,
        "crc32": encoded.crc32,
        "chunks": len(chunks),
        "chunk_payload_size": chunk_payload_size,
    }
    (out / f"{page_name}.summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    typer.echo(json.dumps(summary, indent=2))


@app.command("devices")
def devices() -> None:
    """Scan for tlsr-layout tags (not implemented in Phase 1)."""

    typer.echo("not implemented in Phase 1: BLE scan ships in Phase 6.")


@app.command("push")
def push(
    page_name: str = typer.Argument(...),
    device: str | None = typer.Option(None, "--device"),
) -> None:
    """Push a page to a tag (not implemented in Phase 1)."""

    typer.echo("not implemented in Phase 1: BLE push ships in Phase 6.")


@app.command("serve")
def serve(
    interval: str = typer.Option("60s", "--interval"),
    device_filter: str | None = typer.Option(None, "--device-filter"),
) -> None:
    """Long-running daemon (not implemented in Phase 1)."""

    typer.echo("not implemented in Phase 1: serve ships in Phase 7.")
```

- [ ] **Step 4: Run tests to verify pass**

Run: `cd producer && uv run pytest tests/test_cli_dry_run.py -v`
Expected: 5 passed.

- [ ] **Step 5: Full suite + lint pass**

Run: `cd producer && uv run pytest -q && uv run ruff check . && uv run ruff format --check .`
Expected: all green. If `ruff format --check` reports diffs, run `uv run ruff format .` and re-run.

- [ ] **Step 6: Manual smoke test**

Run: `cd producer && uv run tlsr-layout dry-run example --out /tmp/tlsr-layout-smoke`
Expected: prints summary JSON; `/tmp/tlsr-layout-smoke/` contains `example.json`, `example.zlib`, `example.chunks.bin`, `example.summary.json`. Pipe `example.json | jq` (or `python -m json.tool < example.json`) and eyeball the shape.

- [ ] **Step 7: Commit**

```bash
git add producer/src/tlsr_layout/cli.py producer/tests/test_cli_dry_run.py
git commit -m "feat(producer): typer CLI with working dry-run"
```

---

## Task 9: Coverage gate + dev convenience

**Files:**
- Modify: `producer/pyproject.toml` (add coverage settings)
- Create: `producer/Makefile`

- [ ] **Step 1: Append coverage config to `producer/pyproject.toml`**

```toml
[tool.pytest.ini_options]
testpaths = ["tests"]
addopts = "--cov=tlsr_layout --cov-report=term-missing --cov-fail-under=90"

[tool.coverage.run]
branch = true
source = ["src/tlsr_layout"]
```

- [ ] **Step 2: Create `producer/Makefile`**

```makefile
.PHONY: install test lint fmt smoke

install:
	uv sync --all-extras

test:
	uv run pytest

lint:
	uv run ruff check .
	uv run ruff format --check .

fmt:
	uv run ruff format .
	uv run ruff check --fix .

smoke:
	uv run tlsr-layout dry-run example --out /tmp/tlsr-layout-smoke
```

- [ ] **Step 3: Verify coverage gate**

Run: `cd producer && uv run pytest`
Expected: passes with coverage ≥ 90 %. If under 90 %, the gate fails and the CI-as-local check works. If under 90 % at this stage something is missing — add focused tests for the missed lines (likely in `cli.py` error branches).

- [ ] **Step 4: Commit**

```bash
git add producer/pyproject.toml producer/Makefile
git commit -m "build(producer): pytest-cov gate + dev makefile"
```

---

## Task 10: Phase 1 wrap-up

- [ ] **Step 1: Final sanity sweep**

Run: `cd producer && make lint && make test && make smoke`
Expected: all green; smoke output dumps the summary JSON.

- [ ] **Step 2: Update `producer/README.md` with Phase 1 status**

Replace the file with:

```markdown
# tlsr-layout producer

Mac-side BLE producer for the TLSR8258 layout-DSL e-paper tag. See
`docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md`.

## Status: Phase 1 (skeleton)

Built:
- Layout DSL builders (`tlsr_layout.layout`) — verbatim port of rcpapercolor's helpers
  with palette validation.
- Encode pipeline (`tlsr_layout.encode`) — canonical JSON → zlib → CRC32 → chunk frames.
- xxhash32 `data_ver` (`tlsr_layout.hash`) — matches the value the firmware advertises.
- Wire-format frame builders (`tlsr_layout.protocol`) — BEGIN/ABORT/COMMIT/DATA + ACK parser.
- CLI: `tlsr-layout dry-run <page> --out <dir>` writes JSON + zlib + chunk frames.

Not yet:
- `tlsr-layout devices` / `push` (Phase 6 — bleak BLE central).
- `tlsr-layout serve` daemon (Phase 7).
- Firmware (Phase 2+).

## Dev

```bash
cd producer
uv sync --all-extras
make test
make smoke
uv run tlsr-layout --help
```
```

- [ ] **Step 3: Commit and tag the phase**

```bash
git add producer/README.md
git commit -m "docs(producer): mark Phase 1 done"
git tag tlsr-layout-phase1
```

- [ ] **Step 4: Output the phase summary for the next planner**

Print to stdout (so the dispatching agent can capture it):

```
PHASE 1 COMPLETE
- producer package builds, tests pass, coverage ≥ 90 %.
- dry-run writes example.json/.zlib/.chunks.bin/.summary.json.
- Next: Phase 2 (firmware fork + new GATT service skeleton).
- Hand off: producer/src/tlsr_layout/protocol.py is the spec-authoritative
  byte layout. Firmware must match it byte-for-byte.
```

---

## Self-Review Checklist

- [x] **Spec coverage** — Phase 1 covers spec §8.1 (package layout), §6.1/6.2 (frame builders), §6.3 (ACK parser). Spec §5 (BLE), §6.4 (transfer flow), §7 (firmware), §9 (testing — BLE/power/firmware-native parts) explicitly deferred to later phases.
- [x] **Placeholder scan** — no TBD / TODO / "handle edge cases" left in steps.
- [x] **Type consistency** — `EncodedPage` fields (`canonical_json`, `compressed`, `data_ver`, `crc32`, `uncompressed_len`, `compressed_len`) are referenced identically in tasks 6 and 8. `build_begin` / `build_commit` / `build_data_chunk` / `parse_ack` names match between tasks 7 and 8.
