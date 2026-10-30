"""Push the cached KP4 cards as a deck onto the KEYPOINT keyboard over BLE.

One-shot, stateless: load the card cache (written by the page fetchers), drop
stale cards, then write the whole deck to the keyboard's GATT characteristic
(each card stamped with its page index and the deck total). The firmware stores
the deck and the keyboard's left keys page through it; the launchd job re-runs
this every minute to refresh data and live countdowns.

Countdowns (Claude/Codex reset) are rendered here, at push time, so they stay
live between the 15-minute fetches. The deck order is fixed (keypoint.CARD_ORDER).

Run manually:
    uv run keypoint-push                   # push the deck once, then exit
    uv run keypoint-push --response        # diagnostic writes surface firmware rejects
    uv run keypoint-push --pace 0          # no inter-write delay
    uv run keypoint-push --address <UUID>  # skip discovery
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
    try:
        device = await asyncio.wait_for(_resolve_connected_macos(name), timeout=min(scan_timeout, 5.0))
    except TimeoutError:
        logger.warning("connected KEYPOINT lookup timed out; falling back to BLE scan")
        device = None
    if device:
        logger.info(f"using connected peripheral {device.address} ({device.name})")
        return device
    # Fallback: a fresh, connectable device from a live scanner (mirrors the
    # tlsr discovery path that avoids macOS stale-BLEDevice connect failures).
    try:
        device = await asyncio.wait_for(
            BleakScanner.find_device_by_filter(lambda d, _adv: d.name == name, timeout=scan_timeout),
            timeout=scan_timeout,
        )
    except TimeoutError as exc:
        raise RuntimeError(f"BLE scan for {name!r} timed out after {scan_timeout:g}s") from exc
    if device is None:
        raise RuntimeError(f"no connected or advertising BLE device named {name!r}")
    return device


async def _push(
    name: str,
    address: str | None,
    pace: float,
    max_age_s: int,
    scan_timeout: float,
    response: bool,
) -> None:
    now = dt.datetime.now().astimezone()
    frames = frames_to_push(now=now, max_age_s=max_age_s)
    if not frames:
        logger.warning("no fresh cards in cache — nothing to push (firmware keeps its last/NO DATA state)")
        return

    target = await _resolve_device(name=name, address=address, scan_timeout=scan_timeout)
    async with BleakClient(target, services=[SERVICE_UUID]) as client:
        # Push the whole deck; the firmware stores it and owns page navigation.
        for frame in frames:
            await client.write_gatt_char(CHAR_UUID, frame, response=response)
            logger.info(f"pushed {frame.decode('ascii')}")
            if pace:
                await asyncio.sleep(pace)  # brief pacing for write-without-response reliability
    logger.success(f"pushed deck: {len(frames)} card(s), response={response}")


@app.command()
def run(
    name: str = typer.Option("KEYPOINT", help="BLE device name when --address is unset."),
    address: str | None = typer.Option(None, help="BLE address/UUID to target directly (skips discovery)."),
    pace: float = typer.Option(0.1, min=0.0, help="Seconds between back-to-back deck writes."),
    max_age: int = typer.Option(DEFAULT_MAX_AGE_S, min=60, help="Drop cards older than this many seconds."),
    scan_timeout: float = typer.Option(8.0, min=1.0, help="BLE scan timeout when falling back to discovery."),
    response: bool = typer.Option(
        True, "--response/--no-response", help="Require the firmware to acknowledge each frame."
    ),
) -> None:
    try:
        asyncio.run(
            _push(
                name=name,
                address=address,
                pace=pace,
                max_age_s=max_age,
                scan_timeout=scan_timeout,
                response=response,
            )
        )
    except (BleakError, RuntimeError) as exc:
        logger.error(f"keypoint-push failed: {exc}")
        raise typer.Exit(code=1) from exc


if __name__ == "__main__":
    app()
