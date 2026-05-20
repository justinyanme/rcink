# tlsr-layout — Phase 7: `serve` Daemon & Page Registry

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement the long-running `tlsr-layout serve` daemon: scan continuously, on each tlsr-layout advertisement compute what to serve, push only when stale. Add the page registry (`claude`/`codex`/`calendar`/`todo`) by porting the rcpaper producer's source modules. Persist per-tag state to `~/.config/tlsr-layout/state.json` so restarts don't re-push pages unnecessarily.

**Architecture:** New module `producer/src/tlsr_layout/serve.py` owns the scan loop, the per-tag round-robin page assignment, and the connect-on-stale logic. New modules under `producer/src/tlsr_layout/pages/` mirror `rcpapercolor/producer/rcpaper/pages/` — same source data, BWR-friendly visual choices. State persistence in `producer/src/tlsr_layout/state.py`.

**Tech Stack:** Python ≥3.13, bleak, typer. Optional dependencies for page sources (Anthropic SDK, OpenAI SDK, macOS Calendar/Reminders via `eventkit` or `pyobjc`). Sources copy what already works in rcpaper.

**Spec:** §4.1 (steady-state lifecycle), §8.3 (page assignment).

**Phase boundary:** After Phase 7 the daemon runs unattended: one Mac, one or more tags, automatic page rotation. Tests cover the round-robin logic + state persistence + scan-loop scheduling using a synthetic discovery stream. Page-source fetchers are exercised against recorded fixtures so the tests don't hit real APIs.

---

## File structure

```
producer/
  src/tlsr_layout/
    state.py                          # per-MAC state in ~/.config/tlsr-layout/state.json
    serve.py                          # scan loop + per-tag worker tasks
    pages/
      example.py                      # (existing — Phase 1)
      claude.py                       # ported from rcpaper claude page
      codex.py                        # ported
      calendar.py                     # ported (uses pyobjc/EventKit)
      todo.py                         # ported (Reminders)
      registry.py                     # name → callable lookup
    cli.py                            # serve becomes real
  tests/
    pages/
      test_claude_page.py             # uses recorded fixture
      test_codex_page.py
      test_calendar_page.py
      test_todo_page.py
      test_registry.py
    serve/
      test_state.py
      test_serve_decides_to_push.py
      test_serve_round_robin.py
      test_cli_serve.py
```

---

## Task 1: Per-tag state file

**Files:**
- Create: `producer/src/tlsr_layout/state.py`
- Create: `producer/tests/serve/test_state.py`

- [ ] **Step 1: `state.py`**

```python
"""Per-tag state persisted to ~/.config/tlsr-layout/state.json.

Structure:
{
  "version": 1,
  "tags": {
    "AA:BB:CC:DD:EE:FF": {
      "page_index": 2,
      "last_data_ver": "0xDEADBEEF",
      "last_pushed_at": 1715000000
    }
  }
}
"""

import json
import os
from dataclasses import asdict, dataclass, field
from pathlib import Path


def _config_dir() -> Path:
    return Path(os.environ.get("TLSR_LAYOUT_CONFIG_DIR",
                Path.home() / ".config" / "tlsr-layout"))


@dataclass(slots=True)
class TagState:
    page_index: int = 0
    last_data_ver: int = 0
    last_pushed_at: float = 0.0


@dataclass(slots=True)
class Store:
    version: int = 1
    tags: dict[str, TagState] = field(default_factory=dict)


def load() -> Store:
    p = _config_dir() / "state.json"
    if not p.exists():
        return Store()
    raw = json.loads(p.read_text())
    s = Store(version=raw.get("version", 1))
    for mac, t in raw.get("tags", {}).items():
        s.tags[mac] = TagState(
            page_index=t.get("page_index", 0),
            last_data_ver=int(t.get("last_data_ver", "0"), 0)
                if isinstance(t.get("last_data_ver"), str)
                else int(t.get("last_data_ver", 0)),
            last_pushed_at=float(t.get("last_pushed_at", 0.0)),
        )
    return s


def save(store: Store) -> None:
    d = _config_dir()
    d.mkdir(parents=True, exist_ok=True)
    serialised = {
        "version": store.version,
        "tags": {
            mac: {
                "page_index": t.page_index,
                "last_data_ver": f"0x{t.last_data_ver:08X}",
                "last_pushed_at": t.last_pushed_at,
            }
            for mac, t in store.tags.items()
        },
    }
    (d / "state.json").write_text(json.dumps(serialised, indent=2) + "\n")


def get_or_create(store: Store, mac: str) -> TagState:
    if mac not in store.tags:
        store.tags[mac] = TagState()
    return store.tags[mac]
```

- [ ] **Step 2: Tests**

```python
import os
from pathlib import Path

from tlsr_layout.state import TagState, get_or_create, load, save


def test_roundtrip(tmp_path: Path, monkeypatch):
    monkeypatch.setenv("TLSR_LAYOUT_CONFIG_DIR", str(tmp_path))
    s = load()
    assert s.tags == {}
    t = get_or_create(s, "AA:BB:CC:DD:EE:FF")
    t.page_index = 3
    t.last_data_ver = 0xDEADBEEF
    t.last_pushed_at = 1715000000.0
    save(s)

    reloaded = load()
    assert "AA:BB:CC:DD:EE:FF" in reloaded.tags
    assert reloaded.tags["AA:BB:CC:DD:EE:FF"].page_index == 3
    assert reloaded.tags["AA:BB:CC:DD:EE:FF"].last_data_ver == 0xDEADBEEF


def test_missing_file_returns_empty(tmp_path: Path, monkeypatch):
    monkeypatch.setenv("TLSR_LAYOUT_CONFIG_DIR", str(tmp_path / "nope"))
    assert load().tags == {}
```

- [ ] **Step 3: Commit**

```bash
cd producer && uv run pytest tests/serve/test_state.py -v
git add producer/src/tlsr_layout/state.py producer/tests/serve/__init__.py producer/tests/serve/test_state.py
git commit -m "feat(producer): per-tag state file (round-robin tracking)"
```

---

## Task 2: Page registry

**Files:**
- Create: `producer/src/tlsr_layout/pages/registry.py`
- Create: `producer/tests/pages/test_registry.py`

- [ ] **Step 1: `registry.py`**

```python
"""Registered page builders, keyed by name."""

from typing import Callable

from .example import example_page

# Real fetchers added in Task 3.
PAGES: dict[str, Callable[[], dict]] = {
    "example": example_page,
}


def page_names() -> list[str]:
    return sorted(PAGES)


def build_page(name: str) -> dict:
    builder = PAGES.get(name)
    if builder is None:
        raise KeyError(f"unknown page {name!r}; known: {page_names()}")
    return builder()
```

- [ ] **Step 2: Tests**

```python
import pytest

from tlsr_layout.pages.registry import build_page, page_names


def test_page_names_includes_example():
    assert "example" in page_names()


def test_build_unknown_raises():
    with pytest.raises(KeyError, match="unknown page 'nope'"):
        build_page("nope")


def test_build_example():
    page = build_page("example")
    assert page["type"] == "vstack"
```

- [ ] **Step 3: Commit**

```bash
cd producer && uv run pytest tests/pages/test_registry.py -v
git add producer/src/tlsr_layout/pages/registry.py \
        producer/tests/pages/__init__.py producer/tests/pages/test_registry.py
git commit -m "feat(producer): page registry"
```

---

## Task 3: Port rcpaper page builders

**Files (per source):**
- Create: `producer/src/tlsr_layout/pages/claude.py`
- Create: `producer/src/tlsr_layout/pages/codex.py`
- Create: `producer/src/tlsr_layout/pages/calendar.py`
- Create: `producer/src/tlsr_layout/pages/todo.py`
- Tests: `producer/tests/pages/test_<name>_page.py` with recorded fixtures.

The rcpaper producer fetches usage data from APIs / system services and builds a page. We port each one with two adaptations:

1. **Smaller canvas** — 250×128 vs 400×600. Some pages collapse to a single line of headline + one sparkline + one progress bar.
2. **3-color palette** — use only black / white / red in builders, never yellow / green / blue. (The firmware would map them anyway but it's clearer to commit upstream.)

- [ ] **Step 1: Identify the rcpaper builder for each page**

```bash
ls /Users/rocry/w/_hw/rcpapercolor/producer/rcpaper/pages/
ls /Users/rocry/w/_hw/rcpapercolor/producer/rcpaper/sources/
```

Read each `<name>.py` builder; note the input data shape (a dataclass from `sources/<name>.py`) and the layout-tree it produces.

- [ ] **Step 2: For each of {claude, codex, calendar, todo}:**

For each page, do steps a)–d):

  a) Copy the source fetcher from `rcpapercolor/producer/rcpaper/sources/<name>.py` into `producer/src/tlsr_layout/sources/<name>.py`. The fetcher returns a dataclass (e.g., `ClaudeUsage`).

  b) Port the page builder to `producer/src/tlsr_layout/pages/<name>.py`. Use only BWR colors. Target the 250×128 canvas:
  - title line: `text(size="h1")` (≈28 px high)
  - one sparkline OR one hbar (≈40 px)
  - one or two compact info lines: `text(size="sm")` (≈10 px)
  - tight margins (`pad=4`, `gap=2`)

  c) Add `<name>: <name>.build_page` to `pages/registry.py::PAGES`.

  d) Record a fixture: capture one real source-data result into `producer/tests/pages/fixtures/<name>.json` (run the fetcher once, dump to JSON). Write `test_<name>_page.py` that loads the fixture, calls the builder with that data injected (refactor the builder to accept the dataclass as a parameter), and snapshot-compares the resulting layout tree against a golden in `tests/pages/golden/<name>.json`.

  e) Commit: `git commit -m "feat(producer): port <name> page from rcpaper"`.

The four ports are independent — you can do them in any order. If a source can't be fetched (e.g., calendar requires macOS permissions), record a synthetic fixture by hand for the test, and skip the live fetch in CI by gating it on `TLSR_LAYOUT_LIVE_FETCH=1`.

- [ ] **Step 3: Update the registry**

After all four are ported, the registry should look like:

```python
from .claude import build_page as claude_page
from .codex import build_page as codex_page
from .calendar import build_page as calendar_page
from .todo import build_page as todo_page
from .example import example_page

PAGES = {
    "example": example_page,
    "claude": claude_page,
    "codex": codex_page,
    "calendar": calendar_page,
    "todo": todo_page,
}
```

- [ ] **Step 4: Commit registry update**

```bash
git add producer/src/tlsr_layout/pages/registry.py
git commit -m "feat(producer): register claude/codex/calendar/todo pages"
```

---

## Task 4: Serve daemon

**Files:**
- Create: `producer/src/tlsr_layout/serve.py`
- Create: `producer/tests/serve/test_serve_decides_to_push.py`
- Create: `producer/tests/serve/test_serve_round_robin.py`

- [ ] **Step 1: `serve.py`**

```python
"""Long-running BLE central daemon."""

import asyncio
import logging
import time
from dataclasses import dataclass

from bleak import AdvertisementData, BleakClient, BleakScanner, BLEDevice

from .ble.adv_parser import Advertisement, parse as parse_adv
from .ble.central import PushError, push_page as push_page_via
from .ble.transport import Transport
from .encode import encode_page
from .hash import data_ver as compute_data_ver
from .pages.registry import build_page, page_names
from .state import Store, TagState, get_or_create, load, save

log = logging.getLogger(__name__)

# How often we flush state.json (seconds).
STATE_FLUSH_INTERVAL_S = 30.0


@dataclass(slots=True)
class ServeConfig:
    device_filter: str | None = None
    rotation: list[str] | None = None    # None = all registered pages
    settle_delay_s: float = 2.0


async def serve(config: ServeConfig) -> None:
    store = load()
    rotation = config.rotation or page_names()
    last_state_flush = time.monotonic()
    busy_macs: set[str] = set()

    def _on_adv(device: BLEDevice, adv: AdvertisementData) -> None:
        ad = parse_adv(adv.manufacturer_data)
        if ad is None:
            return
        mac = device.address
        if config.device_filter and mac != config.device_filter:
            return
        if mac in busy_macs:
            return
        asyncio.create_task(_maybe_push(mac, ad, rotation, store, busy_macs))

    log.info("serve loop starting; filter=%s pages=%s", config.device_filter, rotation)
    async with BleakScanner(detection_callback=_on_adv):
        while True:
            await asyncio.sleep(1.0)
            now = time.monotonic()
            if now - last_state_flush > STATE_FLUSH_INTERVAL_S:
                save(store)
                last_state_flush = now


async def _maybe_push(
    mac: str,
    adv: Advertisement,
    rotation: list[str],
    store: Store,
    busy: set[str],
) -> None:
    state = get_or_create(store, mac)
    page_name = rotation[state.page_index % len(rotation)]
    page = build_page(page_name)
    want_ver = compute_data_ver(page)

    if want_ver == adv.data_ver:
        log.debug("%s: in sync (data_ver=0x%08X)", mac, want_ver)
        return

    if adv.low_batt:
        log.info("%s: skipping push, low battery", mac)
        return

    busy.add(mac)
    try:
        async with Transport(BleakClient(mac)) as t:
            try:
                encoded = await push_page_via(t, page)
            except PushError as e:
                log.warning("%s: push failed: %s", mac, e)
                return
        log.info("%s: pushed page=%s data_ver=0x%08X", mac, page_name, encoded.data_ver)
        state.last_data_ver = encoded.data_ver
        state.last_pushed_at = time.time()
        state.page_index = (state.page_index + 1) % len(rotation)
    finally:
        busy.discard(mac)
```

- [ ] **Step 2: Unit tests for the decision logic**

`test_serve_decides_to_push.py`:

```python
import time
from unittest.mock import AsyncMock, patch

import pytest

from tlsr_layout.ble.adv_parser import Advertisement
from tlsr_layout.encode import encode_page
from tlsr_layout.pages.registry import build_page
from tlsr_layout.serve import _maybe_push
from tlsr_layout.state import Store, TagState


@pytest.mark.asyncio
async def test_skips_when_in_sync():
    page = build_page("example")
    enc = encode_page(page)
    adv = Advertisement(
        data_ver=enc.data_ver, battery_mV=2950, temp_c=22, flags=0,
        sync_window=False, low_batt=False, render_err=False,
    )
    store = Store(tags={"AA:BB:CC:DD:EE:FF": TagState(page_index=0)})
    busy: set[str] = set()
    with patch("tlsr_layout.serve.push_page_via", new=AsyncMock()) as p:
        await _maybe_push("AA:BB:CC:DD:EE:FF", adv, ["example"], store, busy)
        p.assert_not_called()


@pytest.mark.asyncio
async def test_skips_on_low_batt():
    adv = Advertisement(0, 2300, 22, 0x02, False, True, False)
    store = Store(tags={"AA:BB:CC:DD:EE:FF": TagState()})
    with patch("tlsr_layout.serve.push_page_via", new=AsyncMock()) as p:
        await _maybe_push("AA:BB:CC:DD:EE:FF", adv, ["example"], store, set())
        p.assert_not_called()


@pytest.mark.asyncio
async def test_pushes_when_stale(monkeypatch):
    page = build_page("example")
    adv = Advertisement(0, 2950, 22, 0, False, False, False)  # data_ver=0, stale
    store = Store(tags={"AA:BB:CC:DD:EE:FF": TagState()})

    pushed = []

    async def fake_push(_t, p):
        pushed.append(p)
        from tlsr_layout.encode import encode_page as _enc
        return _enc(p)

    class FakeClient:
        def __init__(self, *a, **k): pass
        async def __aenter__(self): return self
        async def __aexit__(self, *a): return None
        async def write_gatt_char(self, *a, **k): pass
        async def read_gatt_char(self, *a, **k): return b"\x00" * 18
        async def start_notify(self, *a, **k): pass
        async def stop_notify(self, *a, **k): pass

    monkeypatch.setattr("tlsr_layout.serve.BleakClient", FakeClient)
    monkeypatch.setattr("tlsr_layout.serve.push_page_via", fake_push)

    await _maybe_push("AA:BB:CC:DD:EE:FF", adv, ["example"], store, set())
    assert len(pushed) == 1
    assert store.tags["AA:BB:CC:DD:EE:FF"].page_index == 0
    # next_page rotation moves us to index 0 modulo 1, so it stays 0; verify
    # multi-page rotation in the round-robin test below.
```

`test_serve_round_robin.py`:

```python
import pytest

from tlsr_layout.serve import _maybe_push
from tlsr_layout.state import Store, TagState
from tlsr_layout.ble.adv_parser import Advertisement


@pytest.mark.asyncio
async def test_round_robin_advances_index(monkeypatch):
    async def fake_push(_t, p):
        from tlsr_layout.encode import encode_page as _enc
        return _enc(p)

    class FakeClient:
        def __init__(self, *a, **k): pass
        async def __aenter__(self): return self
        async def __aexit__(self, *a): return None
        async def write_gatt_char(self, *a, **k): pass
        async def read_gatt_char(self, *a, **k): return b"\x00" * 18
        async def start_notify(self, *a, **k): pass
        async def stop_notify(self, *a, **k): pass

    monkeypatch.setattr("tlsr_layout.serve.BleakClient", FakeClient)
    monkeypatch.setattr("tlsr_layout.serve.push_page_via", fake_push)

    store = Store(tags={"AA": TagState()})
    rotation = ["example", "example"]  # same page twice; we only care about index
    adv = Advertisement(0xFFFFFFFF, 2950, 22, 0, False, False, False)

    await _maybe_push("AA", adv, rotation, store, set())
    assert store.tags["AA"].page_index == 1
    await _maybe_push("AA", adv, rotation, store, set())
    assert store.tags["AA"].page_index == 0  # wraps
```

- [ ] **Step 3: CLI command**

In `cli.py`, replace the Phase-1 stub:

```python
@app.command("serve")
def serve_cmd(
    device_filter: str | None = typer.Option(None, "--device-filter"),
    rotation: list[str] = typer.Option(None, "--page", help="Repeatable. Default: all."),
    log_level: str = typer.Option("INFO", "--log-level"),
) -> None:
    import logging

    from .serve import ServeConfig, serve

    logging.basicConfig(level=getattr(logging, log_level.upper(), logging.INFO),
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    cfg = ServeConfig(device_filter=device_filter, rotation=rotation or None)
    asyncio.run(serve(cfg))
```

- [ ] **Step 4: CLI test**

`test_cli_serve.py` — start `serve` in a thread, immediately interrupt, assert it logs the start banner. (Hard to test much more without real BLE; the underlying _maybe_push is the meaty unit.)

- [ ] **Step 5: Run + commit**

```bash
cd producer && uv run pytest -v
git add producer/src/tlsr_layout/serve.py producer/src/tlsr_layout/cli.py \
        producer/tests/serve/test_serve_*.py producer/tests/serve/test_cli_serve.py
git commit -m "feat(producer): serve daemon with adv-driven push-on-stale"
```

---

## Task 5: Phase wrap-up

- [ ] **Step 1: Final sweep**

```bash
cd producer && uv run pytest -v
```

Expected: all green; coverage ≥ 85 % (lowered slightly from 90 % because `serve.py` has BLE-runtime branches that pytest can't fully exercise).

- [ ] **Step 2: Update README + tag**

```bash
git tag -a tlsr-layout-phase7 -m "Phase 7: serve daemon + page registry"
```

Update `producer/README.md`:

```markdown
## Status

- [x] Phase 1 — skeleton
- [x] Phase 6 — BLE central (devices, push)
- [x] Phase 7 — serve daemon + page registry (claude/codex/calendar/todo)

## Daemon

```bash
tlsr-layout serve
# or filter to one device:
tlsr-layout serve --device-filter AA:BB:CC:DD:EE:FF
# or restrict the rotation:
tlsr-layout serve --page claude --page todo
```

State persisted at `~/.config/tlsr-layout/state.json`.
```

- [ ] **Step 3: Phase summary**

```
PHASE 7 COMPLETE
- serve daemon: scans, decides to push on data_ver mismatch, round-robin per tag
- pages: example, claude, codex, calendar, todo (ported from rcpaper)
- state.json persists per-tag page_index + last_data_ver
- Next: Phase 8 — power validation (hardware, manual)
```

---

## Self-Review

- [x] **Spec coverage** — §4.1 (steady-state), §8.3 (page assignment). `serve` settles into the spec's "ignore unless data_ver mismatch" pattern.
- [x] **Placeholder scan** — page-port section is deliberately "do the same thing four times" with explicit sub-steps a–e; no bare TODOs.
- [x] **Type consistency** — `TagState` fields (page_index/last_data_ver/last_pushed_at) consistent between state.py and serve.py. `Advertisement` shape matches Phase 6.
