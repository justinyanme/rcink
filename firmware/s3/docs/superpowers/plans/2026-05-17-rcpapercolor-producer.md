# rcpapercolor Producer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two Mac-side Python scripts (`claude_fetch.py`, `codex_fetch.py`) that pull AI usage from the user's existing OAuth credentials, build a magazine-style layout tree, and POST one event per run to the `rcpapercolor` Axiom dataset.

**Architecture:** A small library (`rcpaper.{layout,axiom,sources,pages}`) under `producer/` driven by two thin entry scripts. Each script is scheduled by `launchd` every 15 min. Tests are pytest, dependencies managed by `uv`.

**Tech Stack:** Python ≥3.13, `uv`, `httpx`, `pydantic` (light), `pytest`, `pytest-httpx` for mocked HTTP, `typer` for the CLI scripts.

**Spec reference:** `docs/superpowers/specs/2026-05-17-rcpapercolor-design.md` (especially "Producer" and "Protocol" sections).

---

## File Structure

```
producer/
├── pyproject.toml
├── README.md
├── .python-version
├── rcpaper/
│   ├── __init__.py
│   ├── layout.py            # DSL helpers that return dict nodes
│   ├── axiom.py             # ingest + query against Axiom
│   ├── sources/
│   │   ├── __init__.py
│   │   ├── claude.py        # OAuth refresh + /api/oauth/usage
│   │   └── codex.py         # OAuth refresh + chatgpt.com wham/usage
│   └── pages/
│       ├── __init__.py
│       ├── claude.py        # build_claude_page(usage, sparkline)
│       └── codex.py         # build_codex_page(usage, sparkline)
├── scripts/
│   ├── claude_fetch.py      # typer entry
│   └── codex_fetch.py       # typer entry
├── launchd/
│   ├── com.rocry.rcpaper.claude.plist.example
│   └── com.rocry.rcpaper.codex.plist.example
└── tests/
    ├── __init__.py
    ├── test_layout.py
    ├── test_axiom.py
    ├── test_sources_claude.py
    ├── test_sources_codex.py
    ├── test_pages_claude.py
    └── test_pages_codex.py
```

Boundary: `layout`/`axiom` are pure utility, `sources` does I/O against provider APIs only, `pages` is pure composition (usage data → layout tree), `scripts` is the orchestration wire-up. Sources never touch Axiom; pages never touch HTTP.

---

## Task 1: Scaffold producer package with uv

**Files:**
- Create: `producer/pyproject.toml`
- Create: `producer/.python-version`
- Create: `producer/rcpaper/__init__.py`
- Create: `producer/tests/__init__.py`
- Create: `producer/README.md`

- [ ] **Step 1: Create pyproject.toml**

```toml
[project]
name = "rcpaper"
version = "0.1.0"
description = "rcpapercolor producer: pushes AI usage dashboards to Axiom."
requires-python = ">=3.13"
dependencies = [
    "httpx>=0.27",
    "typer>=0.12",
    "loguru>=0.7",
]

[project.scripts]
claude-fetch = "scripts.claude_fetch:app"
codex-fetch  = "scripts.codex_fetch:app"

[dependency-groups]
dev = [
    "pytest>=8",
    "pytest-httpx>=0.30",
    "ruff>=0.6",
]

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.hatch.build.targets.wheel]
packages = ["rcpaper", "scripts"]

[tool.pytest.ini_options]
addopts = "-ra -q"
testpaths = ["tests"]
```

- [ ] **Step 2: Pin Python version**

```bash
echo "3.13" > producer/.python-version
```

- [ ] **Step 3: Create package roots**

```python
# producer/rcpaper/__init__.py
"""rcpapercolor producer library."""
__version__ = "0.1.0"
```

```python
# producer/tests/__init__.py
```

- [ ] **Step 4: README skeleton**

```markdown
# rcpaper — producer side

Pulls Claude/Codex usage and pushes layout-tree events to the `rcpapercolor`
Axiom dataset. Each script writes one event per run; the on-device firmware
queries the dataset every ~15 min.

## Setup

```bash
cd producer
uv sync
export AXIOM_TOKEN=xaat-...
export AXIOM_DATASET=rcpapercolor
uv run claude-fetch
uv run codex-fetch
```

See `launchd/` for example plists.
```

- [ ] **Step 5: Sync and verify**

Run: `cd producer && uv sync && uv run python -c "import rcpaper; print(rcpaper.__version__)"`
Expected: `0.1.0`

- [ ] **Step 6: Commit**

```bash
git add producer/
git commit -m "feat(producer): scaffold rcpaper package with uv"
```

---

## Task 2: `rcpaper.layout` — typed DSL helpers (TDD)

**Files:**
- Create: `producer/rcpaper/layout.py`
- Create: `producer/tests/test_layout.py`

These are tiny helper functions that return dicts matching the spec's protocol shape. Done as pure helpers (not classes) because the output IS the JSON.

- [ ] **Step 1: Write the failing tests**

```python
# producer/tests/test_layout.py
from rcpaper.layout import (
    vstack, hstack, spacer, divider, text, hbar, icon, image, sparkline, marker,
)


def test_text_minimal():
    assert text("CLAUDE") == {"type": "text", "value": "CLAUDE"}


def test_text_full():
    assert text(
        "CLAUDE", size="h1", color="white", weight="bold",
        align="center", bg="red", pad=8, max_lines=1,
    ) == {
        "type": "text", "value": "CLAUDE",
        "size": "h1", "color": "white", "weight": "bold",
        "align": "center", "bg": "red", "pad": 8, "max_lines": 1,
    }


def test_vstack_children():
    node = vstack(pad=14, gap=6, align="center", children=[text("A"), text("B")])
    assert node == {
        "type": "vstack", "pad": 14, "gap": 6, "align": "center",
        "children": [
            {"type": "text", "value": "A"},
            {"type": "text", "value": "B"},
        ],
    }


def test_hstack_default():
    assert hstack(children=[]) == {"type": "hstack", "children": []}


def test_spacer_sized_and_flex():
    assert spacer(size=12) == {"type": "spacer", "size": 12}
    assert spacer() == {"type": "spacer"}


def test_divider():
    assert divider(color="black", thickness=2) == {
        "type": "divider", "color": "black", "thickness": 2,
    }


def test_hbar():
    assert hbar(pct=42, color="blue", height=24, label_right="42%") == {
        "type": "hbar", "pct": 42, "color": "blue", "height": 24, "label_right": "42%",
    }


def test_icon_image():
    assert icon("claude", size=20, color="red") == {
        "type": "icon", "name": "claude", "size": 20, "color": "red",
    }
    assert image("logo", width=80, height=80) == {
        "type": "image", "name": "logo", "width": 80, "height": 80,
    }


def test_sparkline_marker():
    assert sparkline([1, 2, 3], color="blue", height=40) == {
        "type": "sparkline", "values": [1, 2, 3], "color": "blue", "height": 40,
    }
    assert marker(pct=80, color="red") == {
        "type": "marker", "pct": 80, "color": "red",
    }


def test_omits_none_keys():
    # We never serialize None — the firmware applies its own defaults
    assert "color" not in text("X")
    assert "size" not in spacer()
```

- [ ] **Step 2: Run tests, confirm they fail**

Run: `cd producer && uv run pytest tests/test_layout.py -v`
Expected: `ModuleNotFoundError: No module named 'rcpaper.layout'`

- [ ] **Step 3: Implement `layout.py`**

```python
# producer/rcpaper/layout.py
"""Tiny DSL: each helper returns a dict matching the rcpapercolor protocol."""
from typing import Any


def _node(t: str, **props: Any) -> dict[str, Any]:
    out: dict[str, Any] = {"type": t}
    for k, v in props.items():
        if v is not None:
            out[k] = v
    return out


def vstack(*, gap=None, pad=None, align=None, children: list[dict] | None = None) -> dict:
    return _node("vstack", gap=gap, pad=pad, align=align, children=children if children is not None else [])


def hstack(*, gap=None, pad=None, align=None, children: list[dict] | None = None) -> dict:
    return _node("hstack", gap=gap, pad=pad, align=align, children=children if children is not None else [])


def spacer(*, size: int | None = None) -> dict:
    return _node("spacer", size=size)


def divider(*, color: str | None = None, thickness: int | None = None) -> dict:
    return _node("divider", color=color, thickness=thickness)


def text(
    value: str,
    *,
    size: str | None = None,
    color: str | None = None,
    weight: str | None = None,
    align: str | None = None,
    bg: str | None = None,
    pad: int | None = None,
    max_lines: int | None = None,
) -> dict:
    return _node(
        "text",
        value=value,
        size=size, color=color, weight=weight, align=align,
        bg=bg, pad=pad, max_lines=max_lines,
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
        pct=pct, color=color, height=height,
        label_left=label_left, label_right=label_right,
    )


def icon(name: str, *, size: int | None = None, color: str | None = None) -> dict:
    return _node("icon", name=name, size=size, color=color)


def image(name: str, *, width: int | None = None, height: int | None = None) -> dict:
    return _node("image", name=name, width=width, height=height)


def sparkline(
    values: list[float],
    *,
    color: str | None = None,
    height: int | None = None,
    baseline: str | int | None = None,
) -> dict:
    return _node("sparkline", values=list(values), color=color, height=height, baseline=baseline)


def marker(*, pct: float, color: str | None = None) -> dict:
    return _node("marker", pct=pct, color=color)
```

- [ ] **Step 4: Run tests, confirm they pass**

Run: `cd producer && uv run pytest tests/test_layout.py -v`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/layout.py producer/tests/test_layout.py
git commit -m "feat(producer): rcpaper.layout DSL with full primitive coverage"
```

---

## Task 3: `rcpaper.axiom` — ingest + query helpers (TDD)

**Files:**
- Create: `producer/rcpaper/axiom.py`
- Create: `producer/tests/test_axiom.py`

Reads `AXIOM_TOKEN` and `AXIOM_DATASET` from env. `ingest()` POSTs one event; `query_apl()` runs an APL string and returns the raw `matches[*].data` list (used by sparkline aggregation).

- [ ] **Step 1: Write the failing tests**

```python
# producer/tests/test_axiom.py
import datetime as dt

import pytest

from rcpaper.axiom import ingest, query_apl, AxiomError


@pytest.fixture
def env(monkeypatch):
    monkeypatch.setenv("AXIOM_TOKEN", "xaat-test")
    monkeypatch.setenv("AXIOM_DATASET", "rcpapercolor")


def test_ingest_posts_event(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        json={"ingested": 1, "failed": 0},
        status_code=200,
    )
    ingest(id="claude", layout={"type": "text", "value": "X"}, order=10)
    req = httpx_mock.get_request()
    assert req.headers["Authorization"] == "Bearer xaat-test"
    assert req.headers["Content-Type"] == "application/json"
    body = req.read()
    import json
    payload = json.loads(body)
    assert isinstance(payload, list) and len(payload) == 1
    ev = payload[0]
    assert ev["kind"] == "page"
    assert ev["id"] == "claude"
    assert ev["order"] == 10
    assert ev["layout"] == {"type": "text", "value": "X"}
    # _time must be ISO8601 UTC
    dt.datetime.fromisoformat(ev["_time"].replace("Z", "+00:00"))


def test_ingest_raises_on_failure(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        json={"ingested": 0, "failed": 1, "failures": [{"error": "bad"}]},
        status_code=200,
    )
    with pytest.raises(AxiomError, match="ingest failed"):
        ingest(id="claude", layout={})


def test_ingest_http_error(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        status_code=401,
        text="unauthorized",
    )
    with pytest.raises(AxiomError, match="401"):
        ingest(id="claude", layout={})


def test_query_apl_returns_rows(httpx_mock, env):
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/_apl?format=legacy",
        method="POST",
        json={
            "matches": [
                {"data": {"id": "claude", "value": 5}},
                {"data": {"id": "claude", "value": 7}},
            ]
        },
    )
    rows = query_apl("['rcpapercolor'] | take 2", lookback=dt.timedelta(hours=1))
    assert rows == [{"id": "claude", "value": 5}, {"id": "claude", "value": 7}]


def test_missing_env_raises():
    with pytest.raises(RuntimeError, match="AXIOM_TOKEN"):
        ingest(id="x", layout={})
```

- [ ] **Step 2: Run tests, confirm they fail**

Run: `cd producer && uv run pytest tests/test_axiom.py -v`
Expected: `ModuleNotFoundError`.

- [ ] **Step 3: Implement `axiom.py`**

```python
# producer/rcpaper/axiom.py
"""Thin Axiom client. Reads AXIOM_TOKEN + AXIOM_DATASET from env."""
from __future__ import annotations

import datetime as dt
import os
from typing import Any

import httpx


class AxiomError(RuntimeError):
    pass


def _env(key: str) -> str:
    v = os.environ.get(key)
    if not v:
        raise RuntimeError(f"{key} not set in environment")
    return v


def _now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def ingest(*, id: str, layout: dict[str, Any], order: int | None = None, ttl_sec: int = 3600) -> None:
    token = _env("AXIOM_TOKEN")
    dataset = _env("AXIOM_DATASET")
    event = {
        "_time": _now_iso(),
        "kind": "page",
        "id": id,
        "ttl_sec": ttl_sec,
        "layout": layout,
    }
    if order is not None:
        event["order"] = order
    url = f"https://api.axiom.co/v1/datasets/{dataset}/ingest"
    r = httpx.post(
        url,
        json=[event],
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        timeout=30,
    )
    if r.status_code != 200:
        raise AxiomError(f"ingest HTTP {r.status_code}: {r.text}")
    body = r.json()
    if body.get("failed", 0):
        raise AxiomError(f"ingest failed: {body.get('failures')}")


def query_apl(apl: str, *, lookback: dt.timedelta = dt.timedelta(days=7)) -> list[dict[str, Any]]:
    """Run an APL query; return a flat list of `data` dicts from matches."""
    token = _env("AXIOM_TOKEN")
    end = dt.datetime.now(dt.timezone.utc)
    start = end - lookback
    body = {
        "apl": apl,
        "startTime": start.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "endTime": end.strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    r = httpx.post(
        "https://api.axiom.co/v1/datasets/_apl?format=legacy",
        json=body,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        timeout=30,
    )
    if r.status_code != 200:
        raise AxiomError(f"query HTTP {r.status_code}: {r.text}")
    payload = r.json()
    return [m.get("data", {}) for m in payload.get("matches", [])]
```

- [ ] **Step 4: Run tests, confirm they pass**

Run: `cd producer && uv run pytest tests/test_axiom.py -v`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/axiom.py producer/tests/test_axiom.py
git commit -m "feat(producer): rcpaper.axiom ingest + query helpers"
```

---

## Task 4: `rcpaper.sources.claude` — Anthropic OAuth + usage (TDD)

**Files:**
- Create: `producer/rcpaper/sources/__init__.py`
- Create: `producer/rcpaper/sources/claude.py`
- Create: `producer/tests/test_sources_claude.py`

Reads `~/.claude/.credentials.json`. Schema (per eInkClaude main.cpp):

```json
{ "claudeAiOauth": {
    "accessToken": "sk-ant-oat01-...",
    "refreshToken": "sk-ant-ort01-...",
    "expiresAt": 1763000000000   /* ms epoch */
}}
```

If `expiresAt - now < 5 min`, refresh; on success, write the new credentials back to the file. Then GET `https://api.anthropic.com/api/oauth/usage` with `anthropic-beta: oauth-2025-04-20` and return a `ClaudeUsage` dataclass.

- [ ] **Step 1: Write the failing tests**

```python
# producer/tests/test_sources_claude.py
import json
import time

import pytest

from rcpaper.sources.claude import (
    ClaudeUsage, fetch_claude_usage, load_creds, save_creds,
)

FRESH = {
    "claudeAiOauth": {
        "accessToken": "sk-ant-oat01-fresh",
        "refreshToken": "sk-ant-ort01-r",
        "expiresAt": int((time.time() + 3600) * 1000),
    }
}

NEAR_EXPIRY = {
    "claudeAiOauth": {
        "accessToken": "sk-ant-oat01-old",
        "refreshToken": "sk-ant-ort01-r",
        "expiresAt": int((time.time() + 60) * 1000),  # 1 min out
    }
}


def write_creds(tmp_path, data):
    p = tmp_path / ".credentials.json"
    p.write_text(json.dumps(data))
    return p


def test_load_and_save_roundtrip(tmp_path):
    p = write_creds(tmp_path, FRESH)
    got = load_creds(p)
    assert got["accessToken"] == "sk-ant-oat01-fresh"
    save_creds(p, {"accessToken": "x", "refreshToken": "y", "expiresAt": 1})
    on_disk = json.loads(p.read_text())
    assert on_disk["claudeAiOauth"]["accessToken"] == "x"


def test_fresh_token_used_directly(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    httpx_mock.add_response(
        url="https://api.anthropic.com/api/oauth/usage",
        json={
            "five_hour": {"utilization": 42.0, "resets_at": "2026-05-17T20:00:00Z"},
            "seven_day": {"utilization": 68.0, "resets_at": "2026-05-24T00:00:00Z"},
            "seven_day_opus":   {"utilization": 18.0, "resets_at": "2026-05-24T00:00:00Z"},
            "seven_day_sonnet": {"utilization": 24.0, "resets_at": "2026-05-24T00:00:00Z"},
        },
    )
    usage = fetch_claude_usage(p)
    assert isinstance(usage, ClaudeUsage)
    assert usage.five_hour_pct == 42.0
    assert usage.seven_day_pct == 68.0
    assert usage.opus_pct == 18.0
    assert usage.sonnet_pct == 24.0
    req = httpx_mock.get_request()
    assert req.headers["Authorization"] == "Bearer sk-ant-oat01-fresh"
    assert req.headers["anthropic-beta"] == "oauth-2025-04-20"


def test_near_expiry_triggers_refresh(httpx_mock, tmp_path):
    p = write_creds(tmp_path, NEAR_EXPIRY)
    # Refresh
    httpx_mock.add_response(
        url="https://console.anthropic.com/v1/oauth/token",
        method="POST",
        json={
            "access_token": "sk-ant-oat01-new",
            "refresh_token": "sk-ant-ort01-new",
            "expires_in": 3600,
        },
    )
    # Usage
    httpx_mock.add_response(
        url="https://api.anthropic.com/api/oauth/usage",
        json={"five_hour": {"utilization": 1, "resets_at": "2026-05-17T20:00:00Z"},
              "seven_day": {"utilization": 2, "resets_at": "2026-05-24T00:00:00Z"}},
    )
    fetch_claude_usage(p)
    # File should be updated
    on_disk = json.loads(p.read_text())["claudeAiOauth"]
    assert on_disk["accessToken"] == "sk-ant-oat01-new"
    assert on_disk["refreshToken"] == "sk-ant-ort01-new"
    assert on_disk["expiresAt"] > int(time.time() * 1000) + 3500_000


def test_usage_reset_text_formatting(httpx_mock, tmp_path):
    p = write_creds(tmp_path, FRESH)
    # ~2h 14m into the future from now
    future = int(time.time()) + 2 * 3600 + 14 * 60
    import datetime as dt
    iso = dt.datetime.fromtimestamp(future, tz=dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    httpx_mock.add_response(
        url="https://api.anthropic.com/api/oauth/usage",
        json={"five_hour": {"utilization": 42.0, "resets_at": iso},
              "seven_day": {"utilization": 68.0, "resets_at": iso}},
    )
    usage = fetch_claude_usage(p)
    assert usage.five_hour_reset.startswith("2h")
```

- [ ] **Step 2: Run tests, confirm they fail**

Run: `cd producer && uv run pytest tests/test_sources_claude.py -v`
Expected: ImportError.

- [ ] **Step 3: Implement `sources/__init__.py` and `sources/claude.py`**

```python
# producer/rcpaper/sources/__init__.py
```

```python
# producer/rcpaper/sources/claude.py
"""Read Claude OAuth from ~/.claude/.credentials.json, refresh + fetch usage."""
from __future__ import annotations

import datetime as dt
import json
import os
from dataclasses import dataclass
from pathlib import Path

import httpx

CREDS_DEFAULT = Path.home() / ".claude" / ".credentials.json"
OAUTH_CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"  # public, from eInkClaude
REFRESH_LEEWAY_SEC = 300  # refresh if expiry < 5 min away


@dataclass(slots=True)
class ClaudeUsage:
    five_hour_pct: float | None
    seven_day_pct: float | None
    opus_pct: float | None
    sonnet_pct: float | None
    five_hour_reset: str   # human-formatted countdown e.g. "2h 14m"
    seven_day_reset: str
    plan: str = "Max"


def load_creds(path: Path = CREDS_DEFAULT) -> dict:
    raw = json.loads(Path(path).read_text())
    return raw["claudeAiOauth"]


def save_creds(path: Path, creds: dict) -> None:
    Path(path).write_text(json.dumps({"claudeAiOauth": creds}, indent=2))
    os.chmod(path, 0o600)


def _refresh(refresh_token: str) -> dict:
    r = httpx.post(
        "https://console.anthropic.com/v1/oauth/token",
        json={
            "grant_type": "refresh_token",
            "refresh_token": refresh_token,
            "client_id": OAUTH_CLIENT_ID,
        },
        timeout=30,
    )
    r.raise_for_status()
    body = r.json()
    return {
        "accessToken": body["access_token"],
        "refreshToken": body["refresh_token"],
        "expiresAt": int((dt.datetime.now(dt.timezone.utc).timestamp() + body["expires_in"]) * 1000),
    }


def _format_countdown(secs: int) -> str:
    if secs <= 0:
        return "now"
    d, rem = divmod(secs, 86400)
    h, rem = divmod(rem, 3600)
    m, _ = divmod(rem, 60)
    if d:
        return f"{d}d {h}h"
    if h:
        return f"{h}h {m}m"
    return f"{m}m"


def _reset_text(iso: str | None) -> str:
    if not iso:
        return ""
    when = dt.datetime.fromisoformat(iso.replace("Z", "+00:00"))
    delta = (when - dt.datetime.now(dt.timezone.utc)).total_seconds()
    return _format_countdown(int(delta))


def fetch_claude_usage(path: Path = CREDS_DEFAULT) -> ClaudeUsage:
    creds = load_creds(path)
    now_ms = int(dt.datetime.now(dt.timezone.utc).timestamp() * 1000)
    if creds["expiresAt"] - now_ms < REFRESH_LEEWAY_SEC * 1000:
        creds = _refresh(creds["refreshToken"])
        save_creds(path, creds)

    r = httpx.get(
        "https://api.anthropic.com/api/oauth/usage",
        headers={
            "Authorization": f"Bearer {creds['accessToken']}",
            "anthropic-beta": "oauth-2025-04-20",
        },
        timeout=30,
    )
    r.raise_for_status()
    data = r.json()

    def pct(key: str) -> float | None:
        v = data.get(key)
        return float(v["utilization"]) if isinstance(v, dict) else None

    def reset(key: str) -> str:
        v = data.get(key)
        return _reset_text(v.get("resets_at")) if isinstance(v, dict) else ""

    return ClaudeUsage(
        five_hour_pct=pct("five_hour"),
        seven_day_pct=pct("seven_day"),
        opus_pct=pct("seven_day_opus"),
        sonnet_pct=pct("seven_day_sonnet"),
        five_hour_reset=reset("five_hour"),
        seven_day_reset=reset("seven_day"),
    )
```

- [ ] **Step 4: Run tests, confirm they pass**

Run: `cd producer && uv run pytest tests/test_sources_claude.py -v`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/sources/ producer/tests/test_sources_claude.py
git commit -m "feat(producer): Claude OAuth refresh + usage fetch"
```

---

## Task 5: `rcpaper.sources.codex` — Codex OAuth + usage (TDD)

**Files:**
- Create: `producer/rcpaper/sources/codex.py`
- Create: `producer/tests/test_sources_codex.py`

Reads `~/.codex/auth.json` (Codex CLI's file). On `last_refresh` older than 8 d, refresh access token via OpenAI token endpoint, persist back. Then GET `https://chatgpt.com/backend-api/wham/usage` with `Authorization: Bearer <access>`.

Per CodexBar docs/codex.md, the file shape:
```json
{ "tokens": { "access_token": "...", "refresh_token": "...", "id_token": "..." },
  "last_refresh": "2026-05-09T10:00:00Z" }
```

- [ ] **Step 1: Write the failing tests**

```python
# producer/tests/test_sources_codex.py
import datetime as dt
import json

import pytest

from rcpaper.sources.codex import CodexUsage, fetch_codex_usage


def write_auth(tmp_path, last_refresh_days_ago: float):
    p = tmp_path / "auth.json"
    last = dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=last_refresh_days_ago)
    p.write_text(json.dumps({
        "tokens": {
            "access_token": "old-access",
            "refresh_token": "rrr",
            "id_token": "iii",
        },
        "last_refresh": last.strftime("%Y-%m-%dT%H:%M:%SZ"),
    }))
    return p


def test_fresh_auth_skips_refresh(httpx_mock, tmp_path):
    p = write_auth(tmp_path, last_refresh_days_ago=1)
    httpx_mock.add_response(
        url="https://chatgpt.com/backend-api/wham/usage",
        json={
            "rate_limits": {
                "primary": {"used_percent": 42.0, "resets_at": "2026-05-17T20:00:00Z",
                            "window_minutes": 300},
                "secondary": {"used_percent": 68.0, "resets_at": "2026-05-24T00:00:00Z",
                              "window_minutes": 10080},
            },
            "credits": {"balance_cents": 1842},
        },
    )
    usage = fetch_codex_usage(p)
    assert isinstance(usage, CodexUsage)
    assert usage.primary_pct == 42.0
    assert usage.secondary_pct == 68.0
    assert usage.credits_dollars == 18.42
    req = httpx_mock.get_request()
    assert req.headers["Authorization"] == "Bearer old-access"


def test_stale_auth_triggers_refresh(httpx_mock, tmp_path):
    p = write_auth(tmp_path, last_refresh_days_ago=9)
    httpx_mock.add_response(
        url="https://auth.openai.com/oauth/token",
        method="POST",
        json={"access_token": "new-access", "refresh_token": "new-rrr", "id_token": "new-iii",
              "expires_in": 3600},
    )
    httpx_mock.add_response(
        url="https://chatgpt.com/backend-api/wham/usage",
        json={"rate_limits": {"primary": {"used_percent": 1.0, "resets_at": "2026-05-17T20:00:00Z",
                                          "window_minutes": 300}},
              "credits": {"balance_cents": 0}},
    )
    fetch_codex_usage(p)
    on_disk = json.loads(p.read_text())
    assert on_disk["tokens"]["access_token"] == "new-access"
    assert on_disk["tokens"]["refresh_token"] == "new-rrr"
```

- [ ] **Step 2: Run tests, confirm they fail**

Run: `cd producer && uv run pytest tests/test_sources_codex.py -v`
Expected: ImportError.

- [ ] **Step 3: Implement `codex.py`**

```python
# producer/rcpaper/sources/codex.py
"""Read Codex OAuth from ~/.codex/auth.json, refresh + fetch usage."""
from __future__ import annotations

import datetime as dt
import json
import os
from dataclasses import dataclass
from pathlib import Path

import httpx

AUTH_DEFAULT = Path.home() / ".codex" / "auth.json"
CODEX_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann"  # public, from Codex CLI
REFRESH_AFTER_DAYS = 8


@dataclass(slots=True)
class CodexUsage:
    primary_pct: float | None
    primary_reset: str
    primary_window_min: int | None
    secondary_pct: float | None
    secondary_reset: str
    secondary_window_min: int | None
    credits_dollars: float | None
    plan: str = "Plus"


def _format_countdown(secs: int) -> str:
    if secs <= 0:
        return "now"
    d, rem = divmod(secs, 86400)
    h, rem = divmod(rem, 3600)
    m, _ = divmod(rem, 60)
    if d:
        return f"{d}d {h}h"
    if h:
        return f"{h}h {m}m"
    return f"{m}m"


def _reset_text(iso: str | None) -> str:
    if not iso:
        return ""
    when = dt.datetime.fromisoformat(iso.replace("Z", "+00:00"))
    delta = (when - dt.datetime.now(dt.timezone.utc)).total_seconds()
    return _format_countdown(int(delta))


def _load(path: Path) -> dict:
    return json.loads(Path(path).read_text())


def _save(path: Path, data: dict) -> None:
    Path(path).write_text(json.dumps(data, indent=2))
    os.chmod(path, 0o600)


def _refresh(refresh_token: str) -> dict:
    r = httpx.post(
        "https://auth.openai.com/oauth/token",
        json={
            "grant_type": "refresh_token",
            "refresh_token": refresh_token,
            "client_id": CODEX_CLIENT_ID,
        },
        timeout=30,
    )
    r.raise_for_status()
    return r.json()


def fetch_codex_usage(path: Path = AUTH_DEFAULT) -> CodexUsage:
    auth = _load(path)
    last = dt.datetime.fromisoformat(auth["last_refresh"].replace("Z", "+00:00"))
    if (dt.datetime.now(dt.timezone.utc) - last).days >= REFRESH_AFTER_DAYS:
        new = _refresh(auth["tokens"]["refresh_token"])
        auth["tokens"] = {
            "access_token": new["access_token"],
            "refresh_token": new["refresh_token"],
            "id_token": new.get("id_token", auth["tokens"].get("id_token", "")),
        }
        auth["last_refresh"] = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        _save(path, auth)

    r = httpx.get(
        "https://chatgpt.com/backend-api/wham/usage",
        headers={"Authorization": f"Bearer {auth['tokens']['access_token']}"},
        timeout=30,
    )
    r.raise_for_status()
    data = r.json()

    rl = data.get("rate_limits", {})

    def pct(key: str) -> float | None:
        v = rl.get(key)
        return float(v["used_percent"]) if isinstance(v, dict) else None

    def reset(key: str) -> str:
        v = rl.get(key)
        return _reset_text(v.get("resets_at")) if isinstance(v, dict) else ""

    def window(key: str) -> int | None:
        v = rl.get(key)
        return int(v["window_minutes"]) if isinstance(v, dict) and "window_minutes" in v else None

    credits = data.get("credits", {})
    cents = credits.get("balance_cents")
    dollars = float(cents) / 100 if isinstance(cents, (int, float)) else None

    return CodexUsage(
        primary_pct=pct("primary"),
        primary_reset=reset("primary"),
        primary_window_min=window("primary"),
        secondary_pct=pct("secondary"),
        secondary_reset=reset("secondary"),
        secondary_window_min=window("secondary"),
        credits_dollars=dollars,
    )
```

- [ ] **Step 4: Run tests, confirm they pass**

Run: `cd producer && uv run pytest tests/test_sources_codex.py -v`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/sources/codex.py producer/tests/test_sources_codex.py
git commit -m "feat(producer): Codex OAuth refresh + usage fetch"
```

---

## Task 6: `rcpaper.pages.claude` — build magazine layout (TDD)

**Files:**
- Create: `producer/rcpaper/pages/__init__.py`
- Create: `producer/rcpaper/pages/claude.py`
- Create: `producer/tests/test_pages_claude.py`

Pure function. Inputs: `ClaudeUsage`, `sparkline: list[float] | None`, `updated_at: datetime`. Output: dict matching the protocol. Follows the magazine direction approved in the spec: red header strip, two KPI rows (5H / 7D), per-model framed cards, sparkline, footer with timestamp.

- [ ] **Step 1: Write the failing tests**

```python
# producer/tests/test_pages_claude.py
import datetime as dt

from rcpaper.pages.claude import build_claude_page
from rcpaper.sources.claude import ClaudeUsage


def make_usage():
    return ClaudeUsage(
        five_hour_pct=42.0, seven_day_pct=68.0,
        opus_pct=18.0, sonnet_pct=24.0,
        five_hour_reset="2h 14m", seven_day_reset="3d 5h",
        plan="Max",
    )


def test_root_is_vstack():
    page = build_claude_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17, 14, 32))
    assert page["type"] == "vstack"
    assert "children" in page


def test_header_is_red_knockout_title():
    page = build_claude_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17, 14, 32))
    header = page["children"][0]
    assert header["type"] == "text"
    assert header["value"] == "CLAUDE"
    assert header["color"] == "white"
    assert header["bg"] == "red"


def test_footer_includes_updated_and_plan():
    page = build_claude_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17, 14, 32))
    footer_text = page["children"][-1]
    assert footer_text["type"] == "text"
    assert "14:32" in footer_text["value"]
    assert "Max" in footer_text["value"]


def test_sparkline_only_added_when_present():
    no_spark = build_claude_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17))
    with_spark = build_claude_page(make_usage(), sparkline=[1, 2, 3, 4, 5, 6, 7], updated_at=dt.datetime(2026, 5, 17))
    assert not any(c["type"] == "sparkline" for c in no_spark["children"])
    sparkline_nodes = [c for c in with_spark["children"] if c["type"] == "sparkline"]
    assert len(sparkline_nodes) == 1
    assert sparkline_nodes[0]["values"] == [1, 2, 3, 4, 5, 6, 7]


def test_missing_model_data_skipped():
    usage = ClaudeUsage(
        five_hour_pct=42.0, seven_day_pct=68.0,
        opus_pct=None, sonnet_pct=None,
        five_hour_reset="2h", seven_day_reset="3d",
    )
    page = build_claude_page(usage, sparkline=None, updated_at=dt.datetime(2026, 5, 17))
    # Per-model row should be omitted entirely
    text_values = [c.get("value") for c in page["children"] if c["type"] == "text"]
    assert "BY MODEL · 7D" not in text_values
```

- [ ] **Step 2: Run tests, confirm they fail**

Run: `cd producer && uv run pytest tests/test_pages_claude.py -v`
Expected: ImportError.

- [ ] **Step 3: Implement `pages/__init__.py` + `pages/claude.py`**

```python
# producer/rcpaper/pages/__init__.py
```

```python
# producer/rcpaper/pages/claude.py
"""Build a Claude usage page in the magazine style."""
from __future__ import annotations

import datetime as dt

from rcpaper.layout import (
    divider, hbar, hstack, spacer, sparkline as spark, text, vstack,
)
from rcpaper.sources.claude import ClaudeUsage


def _kpi_row(label: str, pct: float | None, reset: str, color: str) -> dict:
    return vstack(gap=3, children=[
        hstack(align="center", children=[
            text(label, size="sm", weight="bold"),
            spacer(),
            text(f"resets {reset}" if reset else "", size="sm"),
        ]),
        hstack(gap=8, align="center", children=[
            text(f"{int(pct)}%" if pct is not None else "—", size="h1", color=color),
            hbar(pct=pct if pct is not None else 0, color=color, height=14),
        ]),
    ])


def _model_cell(label: str, pct: float) -> dict:
    color = "red" if pct >= 80 else "green"
    return vstack(pad=4, children=[
        text(label, size="sm", weight="bold"),
        text(f"{int(pct)}%", size="body", color=color),
    ])


def build_claude_page(usage: ClaudeUsage, *, sparkline: list[float] | None,
                      updated_at: dt.datetime) -> dict:
    children: list[dict] = [
        # Red knockout header
        text("CLAUDE", size="h1", color="white", bg="red", pad=8, align="center"),
        spacer(size=6),

        # KPI rows
        _kpi_row("5H WINDOW", usage.five_hour_pct, usage.five_hour_reset, "blue"),
        spacer(size=8),
        _kpi_row("7D WINDOW", usage.seven_day_pct, usage.seven_day_reset, "green"),
    ]

    # Per-model row only when we have data
    model_cells = []
    if usage.opus_pct is not None:
        model_cells.append(_model_cell("Opus", usage.opus_pct))
    if usage.sonnet_pct is not None:
        model_cells.append(_model_cell("Sonnet", usage.sonnet_pct))
    if model_cells:
        children.append(spacer(size=10))
        children.append(text("BY MODEL · 7D", size="sm", weight="bold"))
        children.append(hstack(gap=6, children=model_cells))

    # Sparkline only when provided
    if sparkline:
        children.append(spacer(size=10))
        children.append(text("DAILY USAGE · LAST 7", size="sm", weight="bold"))
        children.append(spark(sparkline, color="blue", height=40))

    # Footer
    children.append(spacer())
    children.append(divider(color="black", thickness=1))
    children.append(text(
        f"{usage.plan} plan · updated {updated_at:%H:%M}",
        size="sm", align="center",
    ))

    return vstack(pad=14, gap=4, children=children)
```

- [ ] **Step 4: Run tests, confirm they pass**

Run: `cd producer && uv run pytest tests/test_pages_claude.py -v`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/pages/ producer/tests/test_pages_claude.py
git commit -m "feat(producer): build_claude_page magazine layout"
```

---

## Task 7: `rcpaper.pages.codex` — build magazine layout (TDD)

**Files:**
- Create: `producer/rcpaper/pages/codex.py`
- Create: `producer/tests/test_pages_codex.py`

Mirrors Task 6's structure but for Codex: header is a green strip, KPI rows are "5H WINDOW" / "WEEKLY", a yellow credits ribbon at the bottom.

- [ ] **Step 1: Write the failing tests**

```python
# producer/tests/test_pages_codex.py
import datetime as dt

from rcpaper.pages.codex import build_codex_page
from rcpaper.sources.codex import CodexUsage


def make_usage():
    return CodexUsage(
        primary_pct=42.0, primary_reset="2h 14m", primary_window_min=300,
        secondary_pct=68.0, secondary_reset="3d 5h", secondary_window_min=10080,
        credits_dollars=18.42,
        plan="Plus",
    )


def test_root_is_vstack_with_green_header():
    page = build_codex_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17))
    assert page["type"] == "vstack"
    header = page["children"][0]
    assert header["value"] == "CODEX"
    assert header["bg"] == "green"


def test_window_labels_match_window_minutes():
    page = build_codex_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17))
    text_values = [c["children"][0]["children"][0]["value"]
                   for c in page["children"]
                   if c["type"] == "vstack" and c.get("children")
                   and c["children"][0].get("type") == "hstack"]
    # primary_window_min=300 → "5H WINDOW", secondary=10080 → "WEEKLY"
    assert "5H WINDOW" in text_values
    assert "WEEKLY" in text_values


def test_credits_ribbon_present():
    page = build_codex_page(make_usage(), sparkline=None, updated_at=dt.datetime(2026, 5, 17))
    # Look for the yellow text node carrying credits info
    credits_nodes = [c for c in page["children"]
                     if c["type"] == "text" and c.get("bg") == "yellow"]
    assert credits_nodes
    assert "$18.42" in credits_nodes[0]["value"]


def test_credits_omitted_when_none():
    usage = CodexUsage(
        primary_pct=42.0, primary_reset="2h", primary_window_min=300,
        secondary_pct=None, secondary_reset="", secondary_window_min=None,
        credits_dollars=None,
    )
    page = build_codex_page(usage, sparkline=None, updated_at=dt.datetime(2026, 5, 17))
    assert not any(c["type"] == "text" and c.get("bg") == "yellow" for c in page["children"])
```

- [ ] **Step 2: Run tests, confirm they fail**

Run: `cd producer && uv run pytest tests/test_pages_codex.py -v`
Expected: ImportError.

- [ ] **Step 3: Implement `pages/codex.py`**

```python
# producer/rcpaper/pages/codex.py
"""Build a Codex usage page in the magazine style."""
from __future__ import annotations

import datetime as dt

from rcpaper.layout import (
    divider, hbar, hstack, spacer, sparkline as spark, text, vstack,
)
from rcpaper.sources.codex import CodexUsage


def _window_label(minutes: int | None) -> str:
    if minutes is None:
        return "WINDOW"
    if minutes <= 60:
        return f"{minutes}M WINDOW"
    if minutes <= 24 * 60:
        return f"{minutes // 60}H WINDOW"
    if minutes == 7 * 24 * 60:
        return "WEEKLY"
    return f"{minutes // (24 * 60)}D WINDOW"


def _kpi_row(label: str, pct: float | None, reset: str, color: str) -> dict:
    return vstack(gap=3, children=[
        hstack(align="center", children=[
            text(label, size="sm", weight="bold"),
            spacer(),
            text(f"resets {reset}" if reset else "", size="sm"),
        ]),
        hstack(gap=8, align="center", children=[
            text(f"{int(pct)}%" if pct is not None else "—", size="h1", color=color),
            hbar(pct=pct if pct is not None else 0, color=color, height=14),
        ]),
    ])


def build_codex_page(usage: CodexUsage, *, sparkline: list[float] | None,
                     updated_at: dt.datetime) -> dict:
    children: list[dict] = [
        text("CODEX", size="h1", color="white", bg="green", pad=8, align="center"),
        spacer(size=6),

        _kpi_row(_window_label(usage.primary_window_min),
                 usage.primary_pct, usage.primary_reset, "blue"),
    ]

    if usage.secondary_pct is not None:
        children.append(spacer(size=8))
        children.append(_kpi_row(_window_label(usage.secondary_window_min),
                                 usage.secondary_pct, usage.secondary_reset, "blue"))

    if sparkline:
        children.append(spacer(size=10))
        children.append(text("DAILY USAGE · LAST 7", size="sm", weight="bold"))
        children.append(spark(sparkline, color="blue", height=40))

    if usage.credits_dollars is not None:
        children.append(spacer(size=10))
        children.append(text(
            f"CREDITS  ${usage.credits_dollars:.2f}",
            size="body", weight="bold", color="black",
            bg="yellow", pad=6, align="center",
        ))

    children.append(spacer())
    children.append(divider(color="black", thickness=1))
    children.append(text(
        f"{usage.plan} plan · updated {updated_at:%H:%M}",
        size="sm", align="center",
    ))

    return vstack(pad=14, gap=4, children=children)
```

- [ ] **Step 4: Run tests, confirm they pass**

Run: `cd producer && uv run pytest tests/test_pages_codex.py -v`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add producer/rcpaper/pages/codex.py producer/tests/test_pages_codex.py
git commit -m "feat(producer): build_codex_page magazine layout"
```

---

## Task 8: `scripts/claude_fetch.py` + `scripts/codex_fetch.py` wire-up

**Files:**
- Create: `producer/scripts/__init__.py`
- Create: `producer/scripts/claude_fetch.py`
- Create: `producer/scripts/codex_fetch.py`

These are thin orchestrators. Sparkline data comes from the producer's own previous Axiom events (each fetch writes a `usage_total` field; the script queries the last 7 days of those for the daily totals).

- [ ] **Step 1: Create entry scripts**

```python
# producer/scripts/__init__.py
```

```python
# producer/scripts/claude_fetch.py
"""Pull Claude usage and push one page event to Axiom."""
from __future__ import annotations

import datetime as dt

import typer
from loguru import logger

from rcpaper.axiom import ingest, query_apl
from rcpaper.pages.claude import build_claude_page
from rcpaper.sources.claude import fetch_claude_usage

app = typer.Typer(add_completion=False, help=__doc__)


def _sparkline_7d() -> list[float]:
    rows = query_apl(
        "['rcpapercolor'] "
        "| where ['kind'] == 'metric' and ['id'] == 'claude.usage' "
        "| summarize avg_pct = avg(['five_hour_pct']) by bin(_time, 1d) "
        "| order by _time asc",
        lookback=dt.timedelta(days=7),
    )
    return [float(r.get("avg_pct", 0) or 0) for r in rows]


@app.command()
def run() -> None:
    usage = fetch_claude_usage()
    logger.info(f"claude usage: 5h={usage.five_hour_pct}% 7d={usage.seven_day_pct}%")

    spark = _sparkline_7d()
    page = build_claude_page(usage, sparkline=spark or None,
                             updated_at=dt.datetime.now())
    ingest(id="claude", layout=page, order=10)

    # Also write a flat metric row so next run can sparkline it
    ingest(id="claude.usage", layout={}, order=None)
    # Note: layout={} is a placeholder; the metric event uses a sibling field set
    # by extending axiom.ingest in a follow-up if needed.


if __name__ == "__main__":
    app()
```

```python
# producer/scripts/codex_fetch.py
"""Pull Codex usage and push one page event to Axiom."""
from __future__ import annotations

import datetime as dt

import typer
from loguru import logger

from rcpaper.axiom import ingest, query_apl
from rcpaper.pages.codex import build_codex_page
from rcpaper.sources.codex import fetch_codex_usage

app = typer.Typer(add_completion=False, help=__doc__)


def _sparkline_7d() -> list[float]:
    rows = query_apl(
        "['rcpapercolor'] "
        "| where ['kind'] == 'metric' and ['id'] == 'codex.usage' "
        "| summarize avg_pct = avg(['primary_pct']) by bin(_time, 1d) "
        "| order by _time asc",
        lookback=dt.timedelta(days=7),
    )
    return [float(r.get("avg_pct", 0) or 0) for r in rows]


@app.command()
def run() -> None:
    usage = fetch_codex_usage()
    logger.info(f"codex usage: primary={usage.primary_pct}% secondary={usage.secondary_pct}%")

    spark = _sparkline_7d()
    page = build_codex_page(usage, sparkline=spark or None,
                            updated_at=dt.datetime.now())
    ingest(id="codex", layout=page, order=20)


if __name__ == "__main__":
    app()
```

- [ ] **Step 2: Extend `rcpaper.axiom` for plain metric events**

The scripts above call `ingest(id="claude.usage", layout={})` as a hack. Add a proper sibling helper so metric rows live alongside page rows with their own fields. Update `rcpaper/axiom.py`:

```python
# Append to producer/rcpaper/axiom.py

def ingest_metric(*, id: str, fields: dict[str, float | int | str]) -> None:
    """Write a flat metric event (no layout). Used for sparkline aggregation."""
    token = _env("AXIOM_TOKEN")
    dataset = _env("AXIOM_DATASET")
    event = {
        "_time": _now_iso(),
        "kind": "metric",
        "id": id,
        **fields,
    }
    url = f"https://api.axiom.co/v1/datasets/{dataset}/ingest"
    r = httpx.post(
        url,
        json=[event],
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        timeout=30,
    )
    if r.status_code != 200:
        raise AxiomError(f"ingest HTTP {r.status_code}: {r.text}")
```

And update scripts to call it:

```python
# In claude_fetch.py run() — replace the placeholder ingest call:
from rcpaper.axiom import ingest_metric
ingest_metric(id="claude.usage", fields={
    "five_hour_pct": usage.five_hour_pct or 0,
    "seven_day_pct": usage.seven_day_pct or 0,
})

# In codex_fetch.py run():
from rcpaper.axiom import ingest_metric
ingest_metric(id="codex.usage", fields={
    "primary_pct": usage.primary_pct or 0,
    "secondary_pct": usage.secondary_pct or 0,
})
```

- [ ] **Step 3: Add a test for `ingest_metric`**

Append to `producer/tests/test_axiom.py`:

```python
def test_ingest_metric(httpx_mock, env):
    from rcpaper.axiom import ingest_metric
    httpx_mock.add_response(
        url="https://api.axiom.co/v1/datasets/rcpapercolor/ingest",
        method="POST",
        json={"ingested": 1, "failed": 0},
    )
    ingest_metric(id="claude.usage", fields={"five_hour_pct": 42.0, "seven_day_pct": 68.0})
    import json
    body = json.loads(httpx_mock.get_request().read())
    assert body[0]["kind"] == "metric"
    assert body[0]["five_hour_pct"] == 42.0
    assert body[0]["seven_day_pct"] == 68.0
```

- [ ] **Step 4: Run all tests**

Run: `cd producer && uv run pytest -v`
Expected: all green.

- [ ] **Step 5: Smoke-test the scripts (manual, requires real Axiom token)**

Run:
```bash
cd producer
export AXIOM_TOKEN=xaat-...
export AXIOM_DATASET=rcpapercolor
uv run claude-fetch
uv run codex-fetch
```
Expected: each prints a log line then exits 0. Verify in the Axiom UI that two `kind=page` events appeared, one with `id=claude`, one with `id=codex`, and one `kind=metric` for each.

- [ ] **Step 6: Commit**

```bash
git add producer/scripts/ producer/rcpaper/axiom.py producer/tests/test_axiom.py
git commit -m "feat(producer): claude_fetch + codex_fetch scripts + metric ingest"
```

---

## Task 9: launchd plists + README finalization

**Files:**
- Create: `producer/launchd/com.rocry.rcpaper.claude.plist.example`
- Create: `producer/launchd/com.rocry.rcpaper.codex.plist.example`
- Modify: `producer/README.md`
- Modify: `.gitignore`

- [ ] **Step 1: Create launchd plist examples**

```xml
<!-- producer/launchd/com.rocry.rcpaper.claude.plist.example -->
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.rocry.rcpaper.claude</string>

  <key>ProgramArguments</key>
  <array>
    <string>/Users/CHANGE_ME/.local/bin/uv</string>
    <string>run</string>
    <string>--directory</string>
    <string>/Users/CHANGE_ME/w/_hw/rcpapercolor/producer</string>
    <string>claude-fetch</string>
  </array>

  <key>EnvironmentVariables</key>
  <dict>
    <key>AXIOM_TOKEN</key>
    <string>xaat-CHANGE_ME</string>
    <key>AXIOM_DATASET</key>
    <string>rcpapercolor</string>
  </dict>

  <key>StartInterval</key>
  <integer>900</integer>

  <key>RunAtLoad</key>
  <true/>

  <key>StandardOutPath</key>
  <string>/tmp/rcpaper-claude.log</string>
  <key>StandardErrorPath</key>
  <string>/tmp/rcpaper-claude.log</string>
</dict>
</plist>
```

(Codex plist is identical with `claude` → `codex` substitutions in `Label`, `ProgramArguments`, log paths.)

```xml
<!-- producer/launchd/com.rocry.rcpaper.codex.plist.example -->
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.rocry.rcpaper.codex</string>
  <key>ProgramArguments</key>
  <array>
    <string>/Users/CHANGE_ME/.local/bin/uv</string>
    <string>run</string>
    <string>--directory</string>
    <string>/Users/CHANGE_ME/w/_hw/rcpapercolor/producer</string>
    <string>codex-fetch</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>AXIOM_TOKEN</key>
    <string>xaat-CHANGE_ME</string>
    <key>AXIOM_DATASET</key>
    <string>rcpapercolor</string>
  </dict>
  <key>StartInterval</key>
  <integer>900</integer>
  <key>RunAtLoad</key>
  <true/>
  <key>StandardOutPath</key>
  <string>/tmp/rcpaper-codex.log</string>
  <key>StandardErrorPath</key>
  <string>/tmp/rcpaper-codex.log</string>
</dict>
</plist>
```

- [ ] **Step 2: Update producer/README.md**

```markdown
# rcpaper — producer side

Pulls Claude/Codex usage from the OAuth files those CLIs already maintain
(`~/.claude/.credentials.json`, `~/.codex/auth.json`), builds a layout-tree
page, and pushes one event per run to the `rcpapercolor` Axiom dataset.

## One-time setup

1. Install `uv` (`brew install uv`) if you don't have it.
2. `cd producer && uv sync`
3. Get an Axiom ingest+query token from <https://axiom.co/settings/api-tokens>.
4. Smoke-test manually:

   ```bash
   export AXIOM_TOKEN=xaat-...
   export AXIOM_DATASET=rcpapercolor
   uv run claude-fetch
   uv run codex-fetch
   ```

   You should see two `kind=page` events appear in the Axiom dataset.

## Schedule with launchd

```bash
# Copy and edit
cp launchd/com.rocry.rcpaper.claude.plist.example \
   ~/Library/LaunchAgents/com.rocry.rcpaper.claude.plist
cp launchd/com.rocry.rcpaper.codex.plist.example \
   ~/Library/LaunchAgents/com.rocry.rcpaper.codex.plist

# Fix `CHANGE_ME` paths + AXIOM_TOKEN in both files
$EDITOR ~/Library/LaunchAgents/com.rocry.rcpaper.claude.plist
$EDITOR ~/Library/LaunchAgents/com.rocry.rcpaper.codex.plist

# Load
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rocry.rcpaper.claude.plist
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.rocry.rcpaper.codex.plist

# Tail logs
tail -f /tmp/rcpaper-{claude,codex}.log
```

## Adding a new dashboard

1. Add a source under `rcpaper/sources/<name>.py` (returns a dataclass).
2. Add a page builder under `rcpaper/pages/<name>.py` (takes the dataclass,
   returns a layout dict).
3. Add an entry script in `scripts/<name>_fetch.py` that wires source → page →
   `ingest(id="<name>", layout=..., order=N)`.
4. Add a launchd plist if you want it scheduled.

The firmware will pick up the new page automatically on its next fetch (the
APL query returns latest-per-id).
```

- [ ] **Step 3: Ensure `.gitignore` covers producer artifacts**

(Already in repo `.gitignore` from spec commit; verify these lines exist:)

```
producer/.venv/
producer/dist/
producer/build/
producer/*.egg-info/
__pycache__/
*.pyc
```

If any missing, append.

- [ ] **Step 4: Run full test suite + ruff**

Run: `cd producer && uv run pytest -v && uv run ruff check . && uv run ruff format --check .`
Expected: all green; format clean.

- [ ] **Step 5: Commit**

```bash
git add producer/launchd/ producer/README.md
git commit -m "docs(producer): launchd examples + setup guide"
```

---

## Self-Review Checklist

- [ ] **Spec coverage:**
  - Producer = Python on Mac, uv ✅ (Task 1)
  - Layout DSL ✅ (Task 2)
  - Axiom ingest + query ✅ (Task 3)
  - Claude source (creds file, refresh, usage fetch) ✅ (Task 4)
  - Codex source (creds file, 8d refresh, usage fetch) ✅ (Task 5)
  - Magazine-style pages ✅ (Tasks 6, 7)
  - Sparkline from producer-side aggregation ✅ (Task 8, `_sparkline_7d`)
  - launchd plists ✅ (Task 9)
  - "Adding new dashboard = drop a script" ✅ (README documents the pattern)

- [ ] **Placeholders:** none — every step has concrete code or commands.

- [ ] **Type consistency:**
  - `ClaudeUsage` / `CodexUsage` dataclass field names are referenced
    identically across tests, sources, and page builders.
  - `ingest()` / `ingest_metric()` / `query_apl()` signatures match across
    Tasks 3, 8, and the script wire-ups.
