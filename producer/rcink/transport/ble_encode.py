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

from rcink.transport.ble_hash import canonical_json, data_ver

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
            raise ValueError(f"too many chunks: payload {len(payload)} B / {max_payload} B exceeds {MAX_CHUNKS}")
        out.append((seq, payload[off : off + max_payload]))
    return out
