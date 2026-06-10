"""Rotate the cached KP2 cards onto the KEYPOINT keyboard display over BLE.

One-shot, stateless: load the card cache (written by the page fetchers), drop
stale cards, then write each fresh card to the keyboard's GATT characteristic,
dwelling a few seconds between cards. The launchd job re-runs this every minute,
so repeated passes produce a continuous rotation without any persistent index.

Countdowns (Claude/Codex reset) are rendered here, at push time, so they stay
live between the 15-minute fetches. The rotation order is fixed (keypoint.CARD_ORDER).

Run manually:
    uv run keypoint-push                 # one rotation, then exit
    uv run keypoint-push --dwell 6        # faster cycling
    uv run keypoint-push --address <UUID> # skip discovery
"""

from __future__ import annotations

import asyncio
import datetime as dt
import platform

import typer
from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice
from bleak.exc import BleakError
from loguru import logger
from rcink.keypoint import CHAR_UUID, SERVICE_UUID, frames_to_push

app = typer.Typer(add_completion=False, help=__doc__)

# A dead fetcher should drop off the rotation, but tolerate a few missed cycles
# (laptop asleep, transient API failure) before a card disappears.
DEFAULT_MAX_AGE_S = 6 * 3600


async def _resolve_connected_macos(name: str) -> BLEDevice | None:
    """The Mac already holds a BLE/HID link to the keyboard, so the connected
    peripheral is retrievable instantly — no scan, no extra advertising window."""
    if platform.system() != "Darwin":
        return None

    from bleak.backends.corebluetooth.CentralManagerDelegate import CentralManagerDelegate
    from CoreBluetooth import CBUUID

    manager = CentralManagerDelegate()  # __init__ does alloc/init internally
    await manager.wait_until_ready()
    peripherals = manager.central_manager.retrieveConnectedPeripheralsWithServices_(
        [CBUUID.UUIDWithString_(SERVICE_UUID)]
    )
    for peripheral in peripherals:
        if name and peripheral.name() != name:
            continue
        return BLEDevice(peripheral.identifier().UUIDString(), peripheral.name(), (peripheral, manager))
    return None


async def _resolve_device(name: str, address: str | None, scan_timeout: float) -> BLEDevice | str:
    if address:
        return address
    if device := await _resolve_connected_macos(name):
        logger.info(f"using connected peripheral {device.address} ({device.name})")
        return device
    # Fallback: a fresh, connectable device from a live scanner (mirrors the
    # tlsr discovery path that avoids macOS stale-BLEDevice connect failures).
    device = await BleakScanner.find_device_by_filter(lambda d, _adv: d.name == name, timeout=scan_timeout)
    if device is None:
        raise RuntimeError(f"no connected or advertising BLE device named {name!r}")
    return device


async def _push(name: str, address: str | None, dwell: float, max_age_s: int, scan_timeout: float) -> None:
    now = dt.datetime.now().astimezone()
    frames = frames_to_push(now=now, max_age_s=max_age_s)
    if not frames:
        logger.warning("no fresh cards in cache — nothing to push (firmware keeps its last/NO DATA state)")
        return

    target = await _resolve_device(name=name, address=address, scan_timeout=scan_timeout)
    async with BleakClient(target, services=[SERVICE_UUID]) as client:
        for index, frame in enumerate(frames):
            await client.write_gatt_char(CHAR_UUID, frame, response=False)
            logger.info(f"pushed {frame.decode('ascii')}")
            if index < len(frames) - 1:
                await asyncio.sleep(dwell)
    logger.success(f"rotation complete: {len(frames)} card(s)")


@app.command()
def run(
    name: str = typer.Option("KEYPOINT", help="BLE device name when --address is unset."),
    address: str | None = typer.Option(None, help="BLE address/UUID to target directly (skips discovery)."),
    dwell: float = typer.Option(12.0, min=0.5, help="Seconds to show each card before the next."),
    max_age: int = typer.Option(DEFAULT_MAX_AGE_S, min=60, help="Drop cards older than this many seconds."),
    scan_timeout: float = typer.Option(8.0, min=1.0, help="BLE scan timeout when falling back to discovery."),
) -> None:
    try:
        asyncio.run(_push(name=name, address=address, dwell=dwell, max_age_s=max_age, scan_timeout=scan_timeout))
    except (BleakError, RuntimeError) as exc:
        logger.error(f"keypoint-push failed: {exc}")
        raise typer.Exit(code=1) from exc


if __name__ == "__main__":
    app()
