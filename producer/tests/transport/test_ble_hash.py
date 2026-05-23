from rcink.transport.ble_hash import canonical_json, data_ver


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
