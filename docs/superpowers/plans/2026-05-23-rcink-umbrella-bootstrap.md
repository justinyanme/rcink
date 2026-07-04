# rcink umbrella bootstrap — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create `~/w/_hw/rcink/` umbrella repo by subtree-merging `rcpapercolor` and `tlsr-layout`, consolidate the Python producer, introduce semantic color tokens, and archive the two old repos. End state: `cd ~/w/_hw/rcink/firmware/s3 && pio run` builds the existing s3 firmware unchanged; `uv run rcink fetch-all` runs the unified producer and ingests pages to Axiom exactly as today; the `tlsr` subtree still builds with its TC32 toolchain.

**Architecture:** Single new git repo at `~/w/_hw/rcink/`. Two `git subtree add` operations import history. Producer moves from `firmware/s3/producer/` up to top-level `producer/` and the Python package renames `rcpaper` → `rcink`. Producer's BLE transport code is lifted from `firmware/tlsr/producer/src/tlsr_layout/ble/` and the rest of that fork is deleted (it's a stale copy of the s3 producer). Semantic color tokens (`accent`, `warning`, `success`, `info`, `muted`) are added alongside the existing concrete tokens (`red`, `yellow`, `green`, `blue`); both work, firmwares map both to the same physical inks.

**Tech Stack:** Python ≥3.13 + uv (producer). C++/PlatformIO (s3). C99/TC32 (tlsr). git subtree for history-preserving import.

**Reference spec:** `docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md` (in current rcpapercolor repo; will end up at `firmware/s3/docs/...` after migration).

---

## Pre-flight check

- [ ] **Step P.1: Verify both source repos clean**

Run:
```bash
cd ~/w/_hw/rcpapercolor && git status --porcelain
cd ~/w/_hw/tlsr-layout && git status --porcelain
```
Expected: both empty (no uncommitted changes). If either is dirty, stop and surface to user.

- [ ] **Step P.2: Verify destination doesn't exist**

Run:
```bash
ls -d ~/w/_hw/rcink 2>/dev/null && echo "EXISTS" || echo "OK"
```
Expected: `OK`. If `EXISTS`, stop — would need user input on what to do.

- [ ] **Step P.3: Verify archive destination doesn't conflict**

Run:
```bash
ls -d ~/w/_hw/_archive 2>/dev/null
```
Expected: directory may or may not exist; create it later if needed. If it exists with `rcpapercolor-*` or `tlsr-layout-*` already, stop.

- [ ] **Step P.4: Verify s3 build green at baseline**

Run:
```bash
cd ~/w/_hw/rcpapercolor && pio run -e m5stack-papercolor 2>&1 | tail -20
```
Expected: build succeeds. This is our "rollback target" snapshot.

- [ ] **Step P.5: Verify tlsr build green at baseline**

Run:
```bash
cd ~/w/_hw/tlsr-layout/firmware && cmake --build build --target TLSR-LAYOUT.bin 2>&1 | tail -20
```
Expected: `.bin` produced. If build dir doesn't exist, configure first:
```bash
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk"
```

- [ ] **Step P.6: Verify producer pytest green at baseline (rcink producer)**

Run:
```bash
cd ~/w/_hw/rcink/producer && uv run pytest 2>&1 | tail -10
```
Expected: all pass.

- [ ] **Step P.7: Verify producer pytest green at baseline (tlsr)**

Run:
```bash
cd ~/w/_hw/tlsr-layout/producer && uv run pytest 2>&1 | tail -10
```
Expected: all pass.

---

## Phase 1 — Bootstrap the umbrella repo

### Task 1.1: Create rcink/ skeleton

**Files:**
- Create: `~/w/_hw/rcink/.gitignore`
- Create: `~/w/_hw/rcink/README.md`
- Create: `~/w/_hw/rcink/AGENTS.md`

- [ ] **Step 1.1.1: Init repo**

```bash
mkdir -p ~/w/_hw/rcink && cd ~/w/_hw/rcink && git init
```

- [ ] **Step 1.1.2: Write .gitignore**

Path: `~/w/_hw/rcink/.gitignore`
Content:
```
# Python
__pycache__/
*.pyc
.pytest_cache/
.ruff_cache/
.venv/
.mypy_cache/
.coverage
htmlcov/

# uv
uv.lock

# PlatformIO
.pio/
.pioenvs/
.piolibdeps/
.vscode/
.cache/

# CMake / TLSR
build/
*.elf
*.bin
*.lst
*.lds

# OS
.DS_Store
Thumbs.db

# Editors
.idea/
*.swp
```

- [ ] **Step 1.1.3: Write README.md**

Path: `~/w/_hw/rcink/README.md`
Content:
```markdown
# rcink

Umbrella repo for rc e-ink dashboard devices. One DSL, one producer, multiple firmware targets.

## Layout

- `dsl/` — DSL spec (JSON schema + cross-device golden render fixtures).
- `producer/` — shared Python producer (pages, sources, transports, CLI).
- `firmware/s3/` — M5Stack PaperColor (ESP32-S3, 400×600, 6-color). Wi-Fi/Axiom.
- `firmware/c3-x4/` — Xteink X4 (ESP32-C3, 800×480, B&W). Wi-Fi/Axiom. *(v1 work in progress)*
- `firmware/tlsr/` — TLSR8258 BWR tag (250×128, 3-color). BLE.
- `docs/superpowers/{specs,plans}/` — design specs and implementation plans.

## Design

See [`docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md`](docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md) for the architecture.

## Build

Each firmware target keeps its own build. The umbrella does not unify them.

```bash
# s3 (PlatformIO)
cd firmware/s3 && pio run -e m5stack-papercolor

# tlsr (CMake + TC32)
cd firmware/tlsr/firmware && cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk"
cmake --build build --target TLSR-LAYOUT.bin

# producer
cd producer && uv run rcink fetch-all
```

## History

This repo was formed on 2026-05-23 by `git subtree add` from:

- `~/w/_hw/rcpapercolor` (archived at `~/w/_hw/_archive/rcpapercolor-2026-05-23`)
- `~/w/_hw/tlsr-layout`  (archived at `~/w/_hw/_archive/tlsr-layout-2026-05-23`)

History of all files imported via subtree is preserved.
```

- [ ] **Step 1.1.4: Write AGENTS.md (minimal pointer file)**

Path: `~/w/_hw/rcink/AGENTS.md`
Content:
```markdown
# rcink — agent guide

This is a umbrella repo for multi-device e-ink dashboards. Three firmware targets share one Python producer and one DSL.

## Where things are

- DSL contract: `dsl/SPEC.md`, `dsl/schema.json`. Read these before touching producer or any firmware.
- Producer: `producer/rcink/` — Python ≥3.13, uv-managed. Run with `uv run rcink ...`.
- Firmwares: each in `firmware/<device>/` with its own build system. Do not unify.
- Specs & plans: `docs/superpowers/{specs,plans}/`.

## Per-firmware references

Each firmware subdir should keep target-specific operator notes in its
`README.md`; read the matching firmware README before changing that target.

## Color tokens

DSL accepts both semantic (`accent`, `warning`, `success`, `info`, `muted`) and concrete (`red`, `yellow`, `green`, `blue`, `black`, `white`) tokens. Each firmware maps both to its physical palette. New page code SHOULD use semantic; legacy concrete remains supported.
```

- [ ] **Step 1.1.5: Initial commit**

```bash
cd ~/w/_hw/rcink
git add .gitignore README.md AGENTS.md
git commit -m "chore: initialize rcink umbrella repo"
```

### Task 1.2: Subtree-merge rcpapercolor → firmware/s3/

- [ ] **Step 1.2.1: Add rcpaper as remote**

```bash
cd ~/w/_hw/rcink
git remote add rcpaper ~/w/_hw/rcpapercolor
git fetch rcpaper
```
Expected: fetches without error.

- [ ] **Step 1.2.2: subtree-add rcpaper at firmware/s3/**

```bash
cd ~/w/_hw/rcink
git subtree add --prefix=firmware/s3 rcpaper main
```
Expected: commit "Add 'firmware/s3/' from commit ..." appears in log.

- [ ] **Step 1.2.3: Remove the remote (no longer needed)**

```bash
cd ~/w/_hw/rcink
git remote remove rcpaper
```

- [ ] **Step 1.2.4: Verify s3 still builds at new path**

```bash
cd ~/w/_hw/rcink/firmware/s3 && pio run -e m5stack-papercolor 2>&1 | tail -10
```
Expected: succeeds. (PlatformIO is self-contained per-directory, no path issues.)

### Task 1.3: Subtree-merge tlsr-layout → firmware/tlsr/

- [ ] **Step 1.3.1: Add tlsr as remote**

```bash
cd ~/w/_hw/rcink
git remote add tlsr ~/w/_hw/tlsr-layout
git fetch tlsr
```

- [ ] **Step 1.3.2: subtree-add tlsr at firmware/tlsr/**

```bash
cd ~/w/_hw/rcink
git subtree add --prefix=firmware/tlsr tlsr main
```

- [ ] **Step 1.3.3: Remove the remote**

```bash
cd ~/w/_hw/rcink
git remote remove tlsr
```

- [ ] **Step 1.3.4: Verify tlsr still builds at new path**

```bash
cd ~/w/_hw/rcink/firmware/tlsr/firmware
cmake -S . -B build -G Ninja \
  -DTOOLCHAIN_PREFIX="$HOME/w/tc32" \
  -DSDK_PREFIX="$HOME/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk" 2>&1 | tail -5
cmake --build build --target TLSR-LAYOUT.bin 2>&1 | tail -5
```
Expected: `TLSR-LAYOUT.bin` produced. (The tlsr CMake project ignores parent path, all relative.)

---

## Phase 2 — Producer consolidation

### Task 2.1: Move producer up to top level

The s3 producer at `firmware/s3/producer/` becomes the canonical top-level `producer/`. The Python package inside (`rcpaper`) is renamed to `rcink` to match the umbrella name.

**Files:**
- Move (git mv): `firmware/s3/producer/` → `producer/`
- Rename package: `producer/rcpaper/` → `producer/rcink/`

- [ ] **Step 2.1.1: git mv producer up**

```bash
cd ~/w/_hw/rcink
git mv firmware/s3/producer producer
```

- [ ] **Step 2.1.2: Rename Python package directory**

```bash
cd ~/w/_hw/rcink
git mv producer/rcpaper producer/rcink
```

- [ ] **Step 2.1.3: Find all `import rcpaper` and `from rcpaper` references**

```bash
cd ~/w/_hw/rcink
rg -l '\b(import|from) rcpaper\b' producer/
```

- [ ] **Step 2.1.4: Replace `rcpaper` → `rcink` across producer/**

Use `sed -i ''` (macOS) on each file from previous step:
```bash
cd ~/w/_hw/rcink
rg -l '\b(import|from) rcpaper\b' producer/ \
  | xargs -I{} sed -i '' \
      -e 's/\bfrom rcpaper\b/from rcink/g' \
      -e 's/\bimport rcpaper\b/import rcink/g' \
      -e 's/rcpaper\./rcink./g' {}
```

- [ ] **Step 2.1.5: Update producer/pyproject.toml — rename project**

Path: `producer/pyproject.toml`
Change every occurrence of `rcpaper` to `rcink` (project name, packages, scripts, entry points).
Inspect with: `rg rcpaper producer/pyproject.toml` — fix each line.

- [ ] **Step 2.1.6: Update CLI entry-point name if present**

In `producer/pyproject.toml` `[project.scripts]` section, the script binary name was likely `rcpaper`. Change to `rcink`:
```toml
[project.scripts]
rcink = "rcink.cli:main"   # adjust path to actual main fn
```

- [ ] **Step 2.1.7: Update tests + imports**

```bash
cd ~/w/_hw/rcink/producer
rg -l rcpaper tests/ 2>/dev/null | xargs -I{} sed -i '' 's/rcpaper/rcink/g' {} || true
```

- [ ] **Step 2.1.8: uv sync the new package**

```bash
cd ~/w/_hw/rcink/producer && uv sync --all-extras
```
Expected: builds environment, installs `rcink` as editable.

- [ ] **Step 2.1.9: Run producer tests**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest 2>&1 | tail -15
```
Expected: all pass. If any reference `rcpaper` still, fix.

- [ ] **Step 2.1.10: Commit**

```bash
cd ~/w/_hw/rcink
git add -A producer/ firmware/s3/
git commit -m "refactor(producer): lift producer to top level, rename rcpaper→rcink"
```

### Task 2.2: Lift BLE transport from tlsr-layout's producer

The tlsr-layout producer's BLE transport (`firmware/tlsr/producer/src/tlsr_layout/ble/`) becomes `producer/rcink/transport/ble/`. The rest of `firmware/tlsr/producer/` is deleted — it's a stale fork of the s3 producer.

**Files:**
- Move: `firmware/tlsr/producer/src/tlsr_layout/ble/` → `producer/rcink/transport/ble/`
- Move (and rename module): `firmware/tlsr/producer/src/tlsr_layout/encode.py` → `producer/rcink/transport/ble_encode.py`
- Move: `firmware/tlsr/producer/src/tlsr_layout/protocol.py` → `producer/rcink/transport/ble_protocol.py`
- Delete: `firmware/tlsr/producer/` (after lift)

- [ ] **Step 2.2.1: Ensure target dir exists**

```bash
cd ~/w/_hw/rcink
mkdir -p producer/rcink/transport
```

- [ ] **Step 2.2.2: Move BLE subdir preserving history**

```bash
cd ~/w/_hw/rcink
git mv firmware/tlsr/producer/src/tlsr_layout/ble producer/rcink/transport/ble
```

- [ ] **Step 2.2.3: Move encode + protocol modules**

```bash
cd ~/w/_hw/rcink
git mv firmware/tlsr/producer/src/tlsr_layout/encode.py    producer/rcink/transport/ble_encode.py
git mv firmware/tlsr/producer/src/tlsr_layout/protocol.py  producer/rcink/transport/ble_protocol.py
git mv firmware/tlsr/producer/src/tlsr_layout/hash.py      producer/rcink/transport/ble_hash.py
```

- [ ] **Step 2.2.4: Move uuids.py if separate**

```bash
cd ~/w/_hw/rcink
test -f firmware/tlsr/producer/src/tlsr_layout/uuids.py && \
  git mv firmware/tlsr/producer/src/tlsr_layout/uuids.py producer/rcink/transport/ble_uuids.py || \
  echo "no uuids.py to move"
```

- [ ] **Step 2.2.5: Rewrite imports in moved BLE code**

```bash
cd ~/w/_hw/rcink
rg -l '\b(import|from) tlsr_layout\b' producer/rcink/transport/ \
  | xargs -I{} sed -i '' \
      -e 's/from tlsr_layout\.ble/from rcink.transport.ble/g' \
      -e 's/from tlsr_layout\.encode/from rcink.transport.ble_encode/g' \
      -e 's/from tlsr_layout\.protocol/from rcink.transport.ble_protocol/g' \
      -e 's/from tlsr_layout\.hash/from rcink.transport.ble_hash/g' \
      -e 's/from tlsr_layout\.uuids/from rcink.transport.ble_uuids/g' \
      -e 's/from tlsr_layout/from rcink/g' \
      -e 's/import tlsr_layout/import rcink/g' {}
```

- [ ] **Step 2.2.6: Delete rest of tlsr-layout producer**

```bash
cd ~/w/_hw/rcink
git rm -r firmware/tlsr/producer
```

- [ ] **Step 2.2.7: Verify producer still imports clean**

```bash
cd ~/w/_hw/rcink/producer
uv run python -c "import rcink; import rcink.transport.ble_encode; import rcink.transport.ble_protocol; print('ok')"
```
Expected: `ok`. If ImportError, find the broken reference and fix.

- [ ] **Step 2.2.8: Add bleak dependency to producer/pyproject.toml**

Inspect tlsr-layout's pyproject.toml for the bleak version, then add to rcink's:
```bash
cd ~/w/_hw/rcink/producer
uv add bleak xxhash  # xxhash was used by tlsr-layout for data_ver
```

- [ ] **Step 2.2.9: Run producer tests**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest 2>&1 | tail -15
```
Expected: previously-passing s3 producer tests still pass. (BLE transport tests may be missing — those will be lifted in a later task if needed.)

- [ ] **Step 2.2.10: Commit**

```bash
cd ~/w/_hw/rcink
git add -A
git commit -m "feat(producer): lift BLE transport from tlsr-layout, drop duplicate producer"
```

### Task 2.3: Add transport router stub

Producer needs a way to know "which device gets which transport." For v1 this is a hand-written registry; later it can be config-driven.

**Files:**
- Create: `producer/rcink/devices.py`
- Modify: `producer/rcink/transport/__init__.py` (router function)

- [ ] **Step 2.3.1: Write devices.py**

Path: `producer/rcink/devices.py`
```python
"""Device registry — what physical units we know about, and how we reach them.

For v1 this is hand-written. v1.1+ may move to a TOML/YAML config.
"""
from dataclasses import dataclass
from typing import Literal


Transport = Literal["axiom", "ble"]


@dataclass(frozen=True, slots=True)
class Device:
    name: str           # short id used in CLI ("s3-home", "x4-desk", "tlsr-001")
    kind: str           # "s3" | "c3-x4" | "tlsr"
    transport: Transport
    ble_mac: str | None = None      # required when transport == "ble"
    axiom_dataset: str | None = None  # defaults to global


# Default registry — overridable via environment in v1.1+.
DEVICES: tuple[Device, ...] = (
    Device(name="s3-home", kind="s3", transport="axiom"),
    # c3-x4 / tlsr devices will be added as they come online
)


def get(name: str) -> Device:
    for d in DEVICES:
        if d.name == name:
            return d
    raise KeyError(f"unknown device {name!r}; known: {[d.name for d in DEVICES]}")
```

- [ ] **Step 2.3.2: Write transport/__init__.py**

Path: `producer/rcink/transport/__init__.py`
```python
"""Transport router — dispatches a page payload to a device's transport."""
from rcink.devices import Device
from rcink.transport import axiom as _axiom


def push(device: Device, page_id: str, layout: dict, order: int = 0) -> None:
    """Push a built page to a device via its configured transport."""
    if device.transport == "axiom":
        _axiom.ingest(id=page_id, layout=layout, order=order, dataset=device.axiom_dataset)
        return
    if device.transport == "ble":
        # Lazy import — bleak heavy, only needed for BLE devices.
        from rcink.transport import ble  # noqa: F401
        raise NotImplementedError("BLE push wiring lands in Plan 3 (rcink-ble-pull-and-goldens)")
    raise ValueError(f"unknown transport {device.transport!r}")
```

- [ ] **Step 2.3.3: Run pytest**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest 2>&1 | tail -10
```
Expected: pass.

- [ ] **Step 2.3.4: Commit**

```bash
cd ~/w/_hw/rcink
git add producer/rcink/devices.py producer/rcink/transport/__init__.py
git commit -m "feat(producer): add device registry + transport router stub"
```

---

## Phase 3 — DSL spec & semantic palette

### Task 3.1: Create dsl/SPEC.md and schema.json

**Files:**
- Create: `dsl/SPEC.md`
- Create: `dsl/schema.json`

- [ ] **Step 3.1.1: Write dsl/SPEC.md**

Path: `~/w/_hw/rcink/dsl/SPEC.md`
Content: copy/condense the "DSL spec" section of
`docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md`. Specifically:

```markdown
# rcink DSL — v1

The DSL is a JSON document describing one page of an e-ink dashboard. Producer emits it; firmware renders it.

## Top-level page

```json
{
  "version": 1,
  "id": "overview",
  "layout": { "type": "vstack", ... }
}
```

| Field     | Type    | Notes                                                          |
|-----------|---------|----------------------------------------------------------------|
| `version` | integer | major version. Devices reject unknown major. v1 = 1.           |
| `id`      | string  | page id; latest-wins per device.                               |
| `layout`  | object  | root node, must be a stack.                                    |

Unknown node types are skipped at render time — they don't bump version.

## Layout nodes

| `type`    | Required props | Optional props                                            |
|-----------|----------------|-----------------------------------------------------------|
| `vstack`  | `children`     | `gap`, `pad`, `align` (`start`/`center`/`end`)            |
| `hstack`  | `children`     | `gap`, `pad`, `align`                                     |
| `spacer`  | —              | `size` (px; omit → flex)                                  |
| `divider` | —              | `color`, `thickness`                                      |

## Content nodes

| `type`      | Required props      | Optional props                                                                |
|-------------|---------------------|-------------------------------------------------------------------------------|
| `text`      | `value`             | `size`, `color`, `weight`, `align`, `bg`, `pad`, `max_lines`                  |
| `hbar`      | `pct` (0–100)       | `color`, `height`, `label_left`, `label_right`                                |
| `icon`      | `name`              | `size`, `color`                                                               |
| `image`     | (`name` or `url`)   | `width`, `height`, `format`                                                   |
| `sparkline` | `values: number[]`  | `color`, `height`, `baseline` (`auto`/`0`)                                    |
| `marker`    | `pct`               | `color` — vertical tick on previous sibling hbar                              |
| `grid`      | `cols`, `rows`, `values: number[]` | `gap`, `width`, `height`                                       |

## Tokens

### Color (semantic + concrete, both accepted)

| Token       | Intent              | s3 (6-ink) | c3-x4 (B&W) | tlsr (BWR) |
|-------------|---------------------|-----------|--------------|------------|
| `black`     | foreground default  | black     | black        | black      |
| `white`     | background          | white     | white        | white      |
| `accent`    | primary emphasis    | red       | black        | red        |
| `warning`   | secondary emphasis  | yellow    | black        | red        |
| `success`   | positive state      | green     | black        | black      |
| `info`      | informational       | blue      | black        | black      |
| `muted`     | de-emphasised fg    | black     | black        | black      |

Concrete tokens (`red`, `yellow`, `green`, `blue`) remain accepted; firmware maps each to the same ink as the semantic alias it most resembles. New code SHOULD use semantic tokens.

### Size

| Token  | Intent                  | s3 px | c3-x4 px | tlsr px |
|--------|-------------------------|-------|----------|---------|
| `H1`   | big number / page title | 60    | 56       | 24      |
| `H2`   | section header          | 32    | 28       | 16      |
| `Body` | default body            | 20    | 18       | 12      |
| `Sm`   | caption / status bar    | 14    | 12       | 10      |

### Weight, Align

| Weight    | Align    |
|-----------|----------|
| `Regular` | `Start`  |
| `Bold`    | `Center` |
|           | `End`    |

## Wire format

UTF-8 JSON, whitespace-stripped (`json.dumps(..., separators=(",", ":"))`).

BLE transport adds zlib + chunking on top — see `producer/rcink/transport/ble_protocol.py`.

## Versioning

`version` is an integer. Bump on breaking changes. Adding new primitives or props is additive and does not bump version — devices skip unknown nodes.
```

- [ ] **Step 3.1.2: Write dsl/schema.json**

Path: `~/w/_hw/rcink/dsl/schema.json`

A minimal JSON Schema that producer + (eventually) firmware can validate against. Cover the most important shape constraints:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "rcink page",
  "type": "object",
  "required": ["version", "id", "layout"],
  "properties": {
    "version": {"type": "integer", "minimum": 1, "maximum": 1},
    "id": {"type": "string", "minLength": 1},
    "layout": {"$ref": "#/$defs/layoutNode"}
  },
  "$defs": {
    "color": {
      "type": "string",
      "enum": ["black", "white", "red", "yellow", "green", "blue",
               "accent", "warning", "success", "info", "muted"]
    },
    "size": {"type": "string", "enum": ["H1", "H2", "Body", "Sm"]},
    "weight": {"type": "string", "enum": ["Regular", "Bold"]},
    "align": {"type": "string", "enum": ["Start", "Center", "End"]},
    "layoutNode": {
      "type": "object",
      "required": ["type"],
      "properties": {
        "type": {"type": "string"},
        "children": {"type": "array", "items": {"$ref": "#/$defs/layoutNode"}}
      }
    }
  }
}
```

- [ ] **Step 3.1.3: Commit**

```bash
cd ~/w/_hw/rcink
git add dsl/SPEC.md dsl/schema.json
git commit -m "docs(dsl): add DSL v1 spec and JSON schema"
```

### Task 3.2: Add semantic color tokens to producer

**Files:**
- Modify: `producer/rcink/palette.py` (add semantic tokens to the accepted set)
- Add: producer unit tests for the new tokens

- [ ] **Step 3.2.1: Read current palette.py**

```bash
cd ~/w/_hw/rcink/producer && cat rcink/palette.py
```
Note the existing `normalize_color()` / accepted token list.

- [ ] **Step 3.2.2: Add semantic tokens to the accepted set**

Modify `producer/rcink/palette.py`: the function that validates color tokens (likely `normalize_color(s)` or `color_from_token(s)`) should accept these in addition to the existing six:
- `accent`, `warning`, `success`, `info`, `muted`

The producer passes the token through as-is; firmware resolves to ink. The producer only validates that the token is in the allowed set.

If the current implementation uses a frozenset or tuple, add the new names there. If it uses an enum, add new enum members.

- [ ] **Step 3.2.3: Write test for new tokens**

Path: `producer/tests/test_palette.py` (extend if exists, else create)
```python
import pytest
from rcink.palette import normalize_color


@pytest.mark.parametrize("token", [
    "black", "white", "red", "yellow", "green", "blue",  # legacy concrete
    "accent", "warning", "success", "info", "muted",     # new semantic
])
def test_normalize_color_accepts_all_v1_tokens(token):
    assert normalize_color(token) == token


def test_normalize_color_rejects_unknown():
    with pytest.raises((ValueError, KeyError)):
        normalize_color("magenta")
```
(Adjust expected exception/behaviour to match the actual `normalize_color` API.)

- [ ] **Step 3.2.4: Run the test**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest tests/test_palette.py -v 2>&1 | tail -15
```
Expected: all pass.

- [ ] **Step 3.2.5: Commit**

```bash
cd ~/w/_hw/rcink
git add producer/rcink/palette.py producer/tests/test_palette.py
git commit -m "feat(palette): accept semantic color tokens alongside concrete"
```

### Task 3.3: Rewrite producer pages to use semantic tokens

This is a one-shot search/replace, but auditable per file. Use a single sed pass then run pytest to confirm golden JSON hashes stay deterministic (or update them deliberately).

Mapping:
- `red`    → `accent`
- `yellow` → `warning`
- `green`  → `success`
- `blue`   → `info`
- `black`  → keep (foreground default)
- `white`  → keep (background)

**Files:**
- Modify: every file under `producer/rcink/pages/*.py` that names a color

- [ ] **Step 3.3.1: Audit which page files contain concrete colors**

```bash
cd ~/w/_hw/rcink/producer
rg --no-heading -n '"(red|yellow|green|blue)"' rcink/pages/
```
Save this list — used as a checklist below.

- [ ] **Step 3.3.2: Mechanical rewrite of color literals**

```bash
cd ~/w/_hw/rcink/producer
rg -l '"(red|yellow|green|blue)"' rcink/pages/ \
  | xargs -I{} sed -i '' \
      -e 's/"red"/"accent"/g' \
      -e 's/"yellow"/"warning"/g' \
      -e 's/"green"/"success"/g' \
      -e 's/"blue"/"info"/g' {}
```

- [ ] **Step 3.3.3: Visual diff review**

```bash
cd ~/w/_hw/rcink && git diff producer/rcink/pages/
```
Confirm: only color string replacements, no other changes. If a builder uses `color="red"` to literally mean "the colour red" (e.g. a hardcoded warning that's meant to be red regardless of palette intent), revert that one instance.

- [ ] **Step 3.3.4: Run pytest — expect goldens to need refresh**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest 2>&1 | tail -30
```
Expected: any page-output snapshot tests will fail because the JSON now contains `accent` etc. instead of `red`.

- [ ] **Step 3.3.5: Refresh page snapshots if the test harness supports it**

If the producer uses `syrupy` or similar:
```bash
cd ~/w/_hw/rcink/producer && uv run pytest --snapshot-update 2>&1 | tail -15
```

If snapshots are manual goldens (JSON files in `tests/fixtures/`), update them by hand using the same sed:
```bash
cd ~/w/_hw/rcink/producer
rg -l '"red"|"yellow"|"green"|"blue"' tests/ \
  | xargs -I{} sed -i '' \
      -e 's/"red"/"accent"/g' \
      -e 's/"yellow"/"warning"/g' \
      -e 's/"green"/"success"/g' \
      -e 's/"blue"/"info"/g' {}
```

- [ ] **Step 3.3.6: Re-run pytest, expect green**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest 2>&1 | tail -10
```

- [ ] **Step 3.3.7: Commit**

```bash
cd ~/w/_hw/rcink
git add -A producer/
git commit -m "refactor(pages): use semantic color tokens (red→accent, yellow→warning, …)"
```

### Task 3.4: Teach s3 firmware to accept semantic tokens

The s3 firmware parses color tokens in `firmware/s3/src/render/palette.cpp` (or `palette.h` — check both). Currently it knows `black`/`white`/`red`/`yellow`/`green`/`blue`. We add `accent`/`warning`/`success`/`info`/`muted`, each mapping to the same E6 ink as the table in the spec.

**Files:**
- Modify: `firmware/s3/src/render/palette.h` (or .cpp)
- Test: `firmware/s3/test/test_palette/` (native unit test, if exists)

- [ ] **Step 3.4.1: Find the existing color lookup**

```bash
cd ~/w/_hw/rcink/firmware/s3
rg -n 'color_from_token|colorFromToken|"black"' src/render/
```
Locate the function that maps token string → palette enum.

- [ ] **Step 3.4.2: Extend the lookup**

Add cases for the five new semantic tokens. For s3 (6-ink Spectra), mapping per the spec table:

```cpp
// Inside color_from_token() in src/render/palette.cpp (or palette.h)
if (strcmp(token, "accent")  == 0) return Color::Red;
if (strcmp(token, "warning") == 0) return Color::Yellow;
if (strcmp(token, "success") == 0) return Color::Green;
if (strcmp(token, "info")    == 0) return Color::Blue;
if (strcmp(token, "muted")   == 0) return Color::Black;
```

(Use the exact enum name + comparison style already in that file.)

- [ ] **Step 3.4.3: Add a unit test (if test/test_proto/ or similar exists)**

If there's already a `test/test_palette/` or `test/test_proto/`, extend it. Otherwise skip (s3 firmware tests are sparse).

Example skeleton (Unity native test):
```cpp
void test_color_from_token_semantic() {
    TEST_ASSERT_EQUAL(Color::Red,    color_from_token("accent"));
    TEST_ASSERT_EQUAL(Color::Yellow, color_from_token("warning"));
    TEST_ASSERT_EQUAL(Color::Green,  color_from_token("success"));
    TEST_ASSERT_EQUAL(Color::Blue,   color_from_token("info"));
    TEST_ASSERT_EQUAL(Color::Black,  color_from_token("muted"));
}
```

- [ ] **Step 3.4.4: Build s3 firmware**

```bash
cd ~/w/_hw/rcink/firmware/s3 && pio run -e m5stack-papercolor 2>&1 | tail -10
```
Expected: builds clean.

- [ ] **Step 3.4.5: Run s3 native tests (if any)**

```bash
cd ~/w/_hw/rcink/firmware/s3 && pio test -e native 2>&1 | tail -15
```
Expected: pass (or "no tests" — that's also fine).

- [ ] **Step 3.4.6: Commit**

```bash
cd ~/w/_hw/rcink
git add firmware/s3/src/render/
git commit -m "feat(s3): accept semantic color tokens (accent/warning/success/info/muted)"
```

### Task 3.5: Teach tlsr firmware to accept semantic tokens

Mirror Task 3.4 in the tlsr firmware. tlsr's palette is at `firmware/tlsr/firmware/src/render/palette.c`. tlsr has only 3 inks (BWR), so multiple tokens map to the same physical color per the spec table.

**Files:**
- Modify: `firmware/tlsr/firmware/src/render/palette.c`

- [ ] **Step 3.5.1: Open and read tlsr palette.c**

```bash
cd ~/w/_hw/rcink/firmware/tlsr/firmware && cat src/render/palette.c | head -50
```

- [ ] **Step 3.5.2: Add semantic token mappings**

Inside whichever function maps token strings → `palette_t` enum, add:

```c
if (strcmp(token, "accent")  == 0) return PAL_RED;
if (strcmp(token, "warning") == 0) return PAL_RED;     // no yellow on BWR
if (strcmp(token, "success") == 0) return PAL_BLACK;   // no green on BWR
if (strcmp(token, "info")    == 0) return PAL_BLACK;   // no blue on BWR
if (strcmp(token, "muted")   == 0) return PAL_BLACK;
```

(Match the existing function name and switch/if-else style.)

- [ ] **Step 3.5.3: Build tlsr firmware**

```bash
cd ~/w/_hw/rcink/firmware/tlsr/firmware
cmake --build build --target TLSR-LAYOUT.bin 2>&1 | tail -10
```
Expected: succeeds.

- [ ] **Step 3.5.4: Run host_test if present**

```bash
cd ~/w/_hw/rcink/firmware/tlsr/firmware
test -d host_test && (cd host_test && cmake --build build 2>&1 | tail -5) || echo "no host_test"
```

If `host_test/build` exists, run any included tests:
```bash
cd ~/w/_hw/rcink/firmware/tlsr/firmware/host_test && ctest --output-on-failure 2>&1 | tail -15 || true
```

- [ ] **Step 3.5.5: Commit**

```bash
cd ~/w/_hw/rcink
git add firmware/tlsr/firmware/src/render/palette.c
git commit -m "feat(tlsr): accept semantic color tokens (BWR-reduced mapping)"
```

---

## Phase 4 — Validation and archive

### Task 4.1: End-to-end producer dry-run

Confirm the unified producer can build all pages with new tokens, against the same data sources as before.

- [ ] **Step 4.1.1: List exposed CLI commands**

```bash
cd ~/w/_hw/rcink/producer && uv run rcink --help 2>&1 | tail -30
```
Expected: shows subcommands including `fetch-all` (or equivalent).

- [ ] **Step 4.1.2: Dry-run page builders (no Axiom push)**

The current rcpaper producer probably has a dry-run flag or a `build` subcommand that prints/saves JSON. Use it:
```bash
cd ~/w/_hw/rcink/producer && uv run rcink build overview --out /tmp/rcink-overview.json 2>&1 | tail -10
cat /tmp/rcink-overview.json | jq '.layout.children[0]' 2>/dev/null | head -10
```
Expected: a JSON tree using semantic color tokens. If the producer doesn't have a `build` subcommand, instead write a tiny one-off:
```bash
cd ~/w/_hw/rcink/producer && uv run python -c "
from rcink.pages import overview
from rcink.sources import claude, codex
# build with no live data — fixtures or empty inputs
result = overview.build()  # adjust call site to match actual signature
import json
print(json.dumps(result, ensure_ascii=False, indent=2)[:600])
"
```

- [ ] **Step 4.1.3: Confirm semantic tokens present**

```bash
grep -E '"(accent|warning|success|info|muted)"' /tmp/rcink-overview.json && echo "OK — semantic tokens present"
```

### Task 4.2: Move design spec + plan into rcink

The spec and this plan were authored in `~/w/_hw/rcpapercolor/docs/` and have
since moved to top-level `docs/superpowers/{specs,plans}/` for the umbrella.
Pre-merge source docs are archived; see `docs/superpowers/ARCHIVE.md`.

**Files:**
- Current: `docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md`
- Current: `docs/superpowers/plans/2026-05-23-rcink-umbrella-bootstrap.md`

- [ ] **Step 4.2.1: Make target dirs**

```bash
cd ~/w/_hw/rcink
mkdir -p docs/superpowers/specs docs/superpowers/plans
```

- [ ] **Step 4.2.2: Move design spec**

```bash
cd ~/w/_hw/rcink
test -f docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md
```

- [ ] **Step 4.2.3: Move this plan**

```bash
cd ~/w/_hw/rcink
test -f docs/superpowers/plans/2026-05-23-rcink-umbrella-bootstrap.md
```

- [ ] **Step 4.2.4: Update the README link**

In `~/w/_hw/rcink/README.md`, the line that points at the spec:
```markdown
See [`docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md`](docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md)
```
Change to:
```markdown
See [`docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md`](docs/superpowers/specs/2026-05-23-rcink-umbrella-design.md)
```

- [ ] **Step 4.2.5: Commit**

```bash
cd ~/w/_hw/rcink
git add docs/ firmware/s3/docs/ README.md
git commit -m "docs: lift umbrella spec + bootstrap plan to top level"
```

### Task 4.3: Final builds, all green

- [ ] **Step 4.3.1: s3 builds**

```bash
cd ~/w/_hw/rcink/firmware/s3 && pio run -e m5stack-papercolor 2>&1 | tail -5
```
Expected: succeeds.

- [ ] **Step 4.3.2: tlsr builds**

```bash
cd ~/w/_hw/rcink/firmware/tlsr/firmware && cmake --build build --target TLSR-LAYOUT.bin 2>&1 | tail -5
```
Expected: succeeds.

- [ ] **Step 4.3.3: Producer tests pass**

```bash
cd ~/w/_hw/rcink/producer && uv run pytest 2>&1 | tail -10
```
Expected: pass.

- [ ] **Step 4.3.4: git status clean**

```bash
cd ~/w/_hw/rcink && git status
```
Expected: nothing to commit.

### Task 4.4: Archive the old repos

These are destructive renames at the filesystem level. The user must not have a running editor/shell session inside either directory during the rename. (Claude Code's working directory is currently inside one of them — we will exit it before renaming.)

- [ ] **Step 4.4.1: Create _archive dir**

```bash
mkdir -p ~/w/_hw/_archive
```

- [ ] **Step 4.4.2: Rename rcpapercolor → _archive/rcpapercolor-2026-05-23**

This MUST run from a working directory that is NOT inside rcpapercolor. Run from `~/w/_hw/rcink/`:
```bash
cd ~/w/_hw/rcink
mv ~/w/_hw/rcpapercolor ~/w/_hw/_archive/rcpapercolor-2026-05-23
echo "Moved rcpapercolor → _archive/rcpapercolor-2026-05-23"
```

- [ ] **Step 4.4.3: Rename tlsr-layout → _archive/tlsr-layout-2026-05-23**

```bash
cd ~/w/_hw/rcink
mv ~/w/_hw/tlsr-layout ~/w/_hw/_archive/tlsr-layout-2026-05-23
echo "Moved tlsr-layout → _archive/tlsr-layout-2026-05-23"
```

- [ ] **Step 4.4.4: Verify archive layout**

```bash
ls -la ~/w/_hw/_archive/
ls -d ~/w/_hw/rcpapercolor 2>/dev/null || echo "rcpapercolor archived ✓"
ls -d ~/w/_hw/tlsr-layout 2>/dev/null || echo "tlsr-layout archived ✓"
ls -d ~/w/_hw/rcink && echo "rcink live ✓"
```

- [ ] **Step 4.4.5: Sanity rebuild from archive paths is broken** (informational)

The archived repos contain the original PlatformIO/CMake project files. They will still build if invoked from inside the archived path. We don't depend on that, but worth knowing.

- [ ] **Step 4.4.6: Update working memory**

Update or create the project memory entry pointing at the new layout so future sessions don't keep referencing the old paths. Save to `~/Documents/ifiles/claude/memory/project_rcink_umbrella.md` and add a one-liner to `MEMORY.md`. (Out of scope for this plan; will be done at session wrap-up via the `done` skill.)

---

## Self-review

Run through the spec's "Migration plan (rough phases)" section. This plan covers spec steps 1–4 and 10. Steps 5 (c3-x4 firmware), 6 (BLE manual-refresh), 7 (X4 buttons), 8 (multi-page slot store), 9 (golden harness) are out of scope here and will be addressed in follow-up plans:

- `2026-05-23-rcink-c3-x4-firmware.md` — covers spec steps 5, 7, 8.
- `2026-05-23-rcink-ble-pull-and-goldens.md` — covers spec steps 6, 9.

Confirmed: every step in this plan has explicit code or commands. No "TBD"s. Type/symbol names (`Device`, `normalize_color`, `color_from_token`, `Color::Red`/`PAL_RED`) consistent across tasks. Per-task commit messages follow Conventional Commits.
