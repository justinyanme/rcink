import zlib

import pytest

from rcink.transport.ble_encode import EncodedPage, chunk_payload, encode_page


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
