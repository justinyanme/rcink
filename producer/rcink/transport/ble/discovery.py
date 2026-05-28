"""Scan for tlsr-layout tags using bleak."""

import asyncio
from dataclasses import dataclass
from typing import Any

from bleak import AdvertisementData, BleakScanner, BLEDevice

from .adv_parser import Advertisement, parse


@dataclass(slots=True, frozen=True)
class DiscoveredTag:
    address: str
    rssi: int
    adv: Advertisement
    device: BLEDevice | None = None


async def discover_tag(*, timeout_s: float = 20.0, address: str | None = None) -> DiscoveredTag | None:
    """Find one tag and return a FRESH, connectable device plus its parsed adv.

    Uses ``find_device_by_filter`` so the scanner stays alive until the device
    is returned connectable. On macOS a ``BLEDevice`` captured by ``scan()``
    (which closes its scanner before returning) goes stale and a later connect
    raises ``BleakDeviceNotFoundError``; this path connects cleanly. The filter
    also captures the advertisement, so callers can dedup on ``adv.data_ver``
    before deciding to connect at all.
    """

    box: dict[str, Any] = {}

    def _filter(device: BLEDevice, adv: AdvertisementData) -> bool:
        if address is not None and device.address != address:
            return False
        parsed = parse(adv.manufacturer_data)
        if parsed is None:
            return False
        box["adv"] = parsed
        box["rssi"] = adv.rssi or 0
        return True

    device = await BleakScanner.find_device_by_filter(_filter, timeout=timeout_s)
    if device is None:
        return None
    return DiscoveredTag(
        address=device.address,
        rssi=box.get("rssi", 0),
        adv=box["adv"],
        device=device,
    )


async def scan(duration_s: float = 10.0) -> dict[str, DiscoveredTag]:
    """Discover tlsr-layout tags during duration_s, keyed by BLE address."""

    found: dict[str, DiscoveredTag] = {}

    def _detect(device: BLEDevice, adv: AdvertisementData) -> None:
        parsed = parse(adv.manufacturer_data)
        if parsed is None:
            return
        found[device.address] = DiscoveredTag(
            address=device.address,
            rssi=adv.rssi or 0,
            adv=parsed,
            device=device,
        )

    async with BleakScanner(detection_callback=_detect):
        await asyncio.sleep(duration_s)

    return found
