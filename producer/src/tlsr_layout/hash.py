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
