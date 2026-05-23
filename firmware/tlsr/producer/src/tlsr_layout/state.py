"""Per-tag daemon state persisted under the user config directory."""

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


def _config_dir() -> Path:
    return Path(
        os.environ.get(
            "TLSR_LAYOUT_CONFIG_DIR",
            Path.home() / ".config" / "tlsr-layout",
        )
    )


@dataclass(slots=True)
class TagState:
    page_index: int = 0
    last_data_ver: int = 0
    last_pushed_at: float = 0.0


@dataclass(slots=True)
class Store:
    version: int = 1
    tags: dict[str, TagState] = field(default_factory=dict)


def _parse_data_ver(value: Any) -> int:
    if isinstance(value, str):
        return int(value, 0)
    return int(value or 0)


def load() -> Store:
    path = _config_dir() / "state.json"
    if not path.exists():
        return Store()

    raw = json.loads(path.read_text())
    store = Store(version=int(raw.get("version", 1)))
    for mac, tag in raw.get("tags", {}).items():
        store.tags[mac] = TagState(
            page_index=int(tag.get("page_index", 0)),
            last_data_ver=_parse_data_ver(tag.get("last_data_ver", 0)),
            last_pushed_at=float(tag.get("last_pushed_at", 0.0)),
        )
    return store


def save(store: Store) -> None:
    config_dir = _config_dir()
    config_dir.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": store.version,
        "tags": {
            mac: {
                "page_index": tag.page_index,
                "last_data_ver": f"0x{tag.last_data_ver:08X}",
                "last_pushed_at": tag.last_pushed_at,
            }
            for mac, tag in sorted(store.tags.items())
        },
    }
    (config_dir / "state.json").write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n")


def get_or_create(store: Store, mac: str) -> TagState:
    if mac not in store.tags:
        store.tags[mac] = TagState()
    return store.tags[mac]
