"""Long-running BLE central daemon."""

import asyncio
import logging
import time
from dataclasses import dataclass

from bleak import AdvertisementData, BleakScanner, BLEDevice

from .ble.adv_parser import Advertisement
from .ble.adv_parser import parse as parse_adv
from .ble.central import PushError
from .ble.central import push_page as push_page_via
from .ble.transport import connect_transport
from .hash import data_ver as compute_data_ver
from .pages.registry import build_page, page_names
from .state import Store, get_or_create, load, save

log = logging.getLogger(__name__)

STATE_FLUSH_INTERVAL_S = 30.0


@dataclass(slots=True)
class ServeConfig:
    device_filter: str | None = None
    rotation: list[str] | None = None
    settle_delay_s: float = 0.0


def _resolve_rotation(rotation: list[str] | None) -> list[str]:
    resolved = rotation or page_names()
    if not resolved:
        raise ValueError("serve rotation must include at least one page")
    known = set(page_names())
    unknown = sorted(set(resolved) - known)
    if unknown:
        raise KeyError(f"unknown page(s) {unknown!r}; known: {page_names()}")
    return resolved


async def serve(config: ServeConfig) -> None:
    store = load()
    rotation = _resolve_rotation(config.rotation)
    last_state_flush = time.monotonic()
    busy_macs: set[str] = set()
    tasks: set[asyncio.Task[None]] = set()

    def schedule(device: BLEDevice, adv_data: AdvertisementData) -> None:
        adv = parse_adv(adv_data.manufacturer_data)
        if adv is None:
            return
        mac = device.address
        if config.device_filter and mac != config.device_filter:
            return
        if mac in busy_macs:
            return

        task = asyncio.create_task(
            _maybe_push_and_save(
                mac,
                adv,
                rotation,
                store,
                busy_macs,
                device_ref=device,
                settle_delay_s=config.settle_delay_s,
            )
        )
        tasks.add(task)
        task.add_done_callback(tasks.discard)

    log.info("serve loop starting; filter=%s pages=%s", config.device_filter, rotation)
    async with BleakScanner(detection_callback=schedule):
        while True:
            await asyncio.sleep(1.0)
            now = time.monotonic()
            if now - last_state_flush >= STATE_FLUSH_INTERVAL_S:
                save(store)
                last_state_flush = now


async def _maybe_push_and_save(
    mac: str,
    adv: Advertisement,
    rotation: list[str],
    store: Store,
    busy: set[str],
    *,
    device_ref: BLEDevice | None = None,
    settle_delay_s: float,
) -> None:
    changed = await _maybe_push(
        mac,
        adv,
        rotation,
        store,
        busy,
        device_ref=device_ref,
        settle_delay_s=settle_delay_s,
    )
    if changed:
        save(store)


async def _maybe_push(
    mac: str,
    adv: Advertisement,
    rotation: list[str],
    store: Store,
    busy: set[str],
    *,
    device_ref: BLEDevice | None = None,
    settle_delay_s: float = 0.0,
) -> bool:
    state = get_or_create(store, mac)
    page_name = rotation[state.page_index % len(rotation)]
    page = build_page(page_name)
    want_ver = compute_data_ver(page)

    if want_ver == adv.data_ver:
        log.debug("%s: in sync (data_ver=0x%08X)", mac, want_ver)
        return False
    if adv.low_batt:
        log.info("%s: skipping push, low battery", mac)
        return False
    if mac in busy:
        return False

    busy.add(mac)
    try:
        if settle_delay_s > 0:
            await asyncio.sleep(settle_delay_s)
        async with connect_transport(mac, device=device_ref) as transport:
            try:
                encoded = await push_page_via(transport, page)
            except PushError as exc:
                log.warning("%s: push failed: %s", mac, exc)
                return False
        log.info("%s: pushed page=%s data_ver=0x%08X", mac, page_name, encoded.data_ver)
        state.last_data_ver = encoded.data_ver
        state.last_pushed_at = time.time()
        state.page_index = (state.page_index + 1) % len(rotation)
        return True
    finally:
        busy.discard(mac)
