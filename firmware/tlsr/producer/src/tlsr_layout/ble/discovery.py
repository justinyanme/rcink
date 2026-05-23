"""Scan for tlsr-layout tags using bleak."""

import asyncio
from dataclasses import dataclass

from bleak import AdvertisementData, BleakScanner, BLEDevice

from .adv_parser import Advertisement, parse


@dataclass(slots=True, frozen=True)
class DiscoveredTag:
    address: str
    rssi: int
    adv: Advertisement
    device: BLEDevice | None = None


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
