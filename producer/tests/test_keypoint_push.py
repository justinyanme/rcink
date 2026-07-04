import asyncio

import pytest
from scripts import keypoint_push


@pytest.mark.asyncio
async def test_resolve_device_scan_timeout_also_bounds_connected_lookup(monkeypatch):
    async def hung_connected_lookup(name):
        await asyncio.sleep(1)
        return None

    async def fake_scan(predicate, timeout):
        return "scan-device"

    monkeypatch.setattr(keypoint_push, "_resolve_connected_macos", hung_connected_lookup)
    monkeypatch.setattr(keypoint_push.BleakScanner, "find_device_by_filter", fake_scan)

    device = await keypoint_push._resolve_device(name="KEYPOINT", address=None, scan_timeout=0.01)

    assert device == "scan-device"


@pytest.mark.asyncio
async def test_resolve_device_scan_timeout_is_enforced(monkeypatch):
    async def no_connected_lookup(name):
        return None

    async def hung_scan(predicate, timeout):
        await asyncio.sleep(1)
        return None

    monkeypatch.setattr(keypoint_push, "_resolve_connected_macos", no_connected_lookup)
    monkeypatch.setattr(keypoint_push.BleakScanner, "find_device_by_filter", hung_scan)

    with pytest.raises(RuntimeError, match="BLE scan.*timed out"):
        await keypoint_push._resolve_device(name="KEYPOINT", address=None, scan_timeout=0.01)
