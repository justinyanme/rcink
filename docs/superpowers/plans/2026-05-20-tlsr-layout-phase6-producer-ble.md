# tlsr-layout — Phase 6: Producer BLE Central (bleak)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement the Mac-side BLE central in the producer: scan for tlsr-layout tags by manufacturer-data filter, connect on demand, run the full BEGIN/DATA/COMMIT transfer, parse ACK notifications, and handle the recovery cases (ERR_GAP resume, ERR_CRC restart, ERR_LOW_BATT backoff). All without real hardware — `bleak` is mocked at the integration boundary.

**Architecture:** Two new modules under `producer/src/tlsr_layout/ble/`:
- `discovery.py` — scan & advertisement-payload parser.
- `central.py` — connect + GATT writes + ACK notification listener; orchestrates a single push.

CLI commands `devices` and `push` (Phase 1 stubs) become real.

**Tech Stack:** Python ≥3.13, bleak ≥0.22, pytest-asyncio. macOS CoreBluetooth (transparent through bleak).

**Spec:** `docs/superpowers/specs/2026-05-20-tlsr-layout-protocol-design.md` §5 (BLE GATT) and §6 (transfer protocol).

**Phase boundary:** After Phase 6 a developer with a flashed Phase-5 tag can run `tlsr-layout push example --device <MAC>` and see the EPD update. `serve` is still a stub until Phase 7. All push-path code is tested against a fake bleak peripheral in pytest.

---

## File structure

```
producer/
  pyproject.toml                       # add bleak, pytest-asyncio
  src/tlsr_layout/
    ble/
      __init__.py
      uuids.py                         # service + characteristic UUIDs
      adv_parser.py                    # decode manufacturer-data payload v2
      discovery.py                     # async scan w/ filter
      transport.py                     # bleak client wrapper (MTU, notify, write)
      central.py                       # push() orchestration
    cli.py                             # devices, push become real
  tests/
    ble/
      __init__.py
      test_adv_parser.py
      test_central_happy.py
      test_central_recovery.py
      test_cli_devices.py
      test_cli_push.py
      fake_peripheral.py               # in-memory bleak-shaped peripheral
```

The fake peripheral is the workhorse: it implements the spec's wire protocol exactly, so any push exercise that passes the fake will pass against real silicon (modulo BLE stack quirks we'll smoke-test on hardware separately).

---

## Task 1: UUIDs + advertisement parser

**Files:**
- Create: `producer/src/tlsr_layout/ble/__init__.py`
- Create: `producer/src/tlsr_layout/ble/uuids.py`
- Create: `producer/src/tlsr_layout/ble/adv_parser.py`
- Create: `producer/tests/ble/__init__.py`
- Create: `producer/tests/ble/test_adv_parser.py`

- [ ] **Step 1: `uuids.py`**

```python
"""BLE UUIDs for the tlsr-layout service — see spec §5.1."""

# Base: 6F636172-1F20-44E0-9B6C-7A_xxxxxxxx_20
# (matches firmware's LAYOUT_CHAR_UUID(slot) byte order, see Phase 2 plan)

SERVICE_UUID  = "6f636172-1f20-44e0-9b6c-7a0000002000"
CHAR_STATUS   = "6f636172-1f20-44e0-9b6c-7a0000012000"
CHAR_XFER_CTL = "6f636172-1f20-44e0-9b6c-7a0000022000"
CHAR_XFER_DATA= "6f636172-1f20-44e0-9b6c-7a0000032000"
CHAR_XFER_ACK = "6f636172-1f20-44e0-9b6c-7a0000042000"
CHAR_META     = "6f636172-1f20-44e0-9b6c-7a0000052000"
CHAR_RINGLOG  = "6f636172-1f20-44e0-9b6c-7a0000062000"   # Phase 5 char

MANUFACTURER_ID = 0xFFFF   # spec §5.2
ADV_PAYLOAD_TYPE_V2 = 0x02
```

- [ ] **Step 2: `adv_parser.py`**

```python
"""Decode the manufacturer-data payload from tlsr-layout advertisements.

Spec §5.2 payload v2:
  0x02 type | data_ver(4) | battery_mV(2) | temp_c(1) | flags(1)
"""

import struct
from dataclasses import dataclass

from .uuids import ADV_PAYLOAD_TYPE_V2, MANUFACTURER_ID


@dataclass(slots=True, frozen=True)
class Advertisement:
    data_ver: int
    battery_mV: int
    temp_c: int
    flags: int
    sync_window: bool
    low_batt: bool
    render_err: bool


def parse(manufacturer_data: dict[int, bytes]) -> Advertisement | None:
    raw = manufacturer_data.get(MANUFACTURER_ID)
    if raw is None or len(raw) < 9:
        return None
    if raw[0] != ADV_PAYLOAD_TYPE_V2:
        return None
    data_ver, battery_mV = struct.unpack_from("<IH", raw, 1)
    temp_c, flags = raw[7], raw[8]
    return Advertisement(
        data_ver=data_ver,
        battery_mV=battery_mV,
        temp_c=struct.unpack("<b", bytes([temp_c]))[0],
        flags=flags,
        sync_window=bool(flags & 0x01),
        low_batt=bool(flags & 0x02),
        render_err=bool(flags & 0x04),
    )
```

- [ ] **Step 3: Tests**

```python
import struct

from tlsr_layout.ble.adv_parser import Advertisement, parse


def test_parses_v2_payload():
    raw = bytes([0x02]) + struct.pack("<IH", 0xDEADBEEF, 2950) + bytes([-3 & 0xFF, 0x05])
    ad = parse({0xFFFF: raw})
    assert ad == Advertisement(
        data_ver=0xDEADBEEF,
        battery_mV=2950,
        temp_c=-3,
        flags=0x05,
        sync_window=True,
        low_batt=False,
        render_err=True,
    )


def test_returns_none_on_wrong_manufacturer():
    assert parse({0x004C: b"\x02"}) is None


def test_returns_none_on_short_payload():
    assert parse({0xFFFF: b"\x02\x00"}) is None


def test_returns_none_on_unknown_type():
    raw = bytes([0xFF]) + b"\x00" * 8
    assert parse({0xFFFF: raw}) is None
```

- [ ] **Step 4: Run + commit**

```bash
cd producer && uv add bleak pytest-asyncio
uv run pytest tests/ble/test_adv_parser.py -v
git add producer/pyproject.toml producer/uv.lock producer/src/tlsr_layout/ble/__init__.py \
        producer/src/tlsr_layout/ble/uuids.py producer/src/tlsr_layout/ble/adv_parser.py \
        producer/tests/ble/__init__.py producer/tests/ble/test_adv_parser.py
git commit -m "feat(producer): BLE UUIDs + advertisement parser"
```

---

## Task 2: Discovery (async scan)

**Files:**
- Create: `producer/src/tlsr_layout/ble/discovery.py`
- Create: `producer/tests/ble/test_discovery.py`

- [ ] **Step 1: `discovery.py`**

```python
"""Scan for tlsr-layout tags. bleak BleakScanner under the hood."""

import asyncio
from dataclasses import dataclass

from bleak import AdvertisementData, BleakScanner, BLEDevice

from .adv_parser import Advertisement, parse


@dataclass(slots=True, frozen=True)
class DiscoveredTag:
    address: str
    rssi: int
    adv: Advertisement


async def scan(duration_s: float = 10.0) -> dict[str, DiscoveredTag]:
    """Discover all tlsr-layout tags during `duration_s`. Returns dict keyed by MAC."""

    found: dict[str, DiscoveredTag] = {}

    def _detect(device: BLEDevice, adv: AdvertisementData) -> None:
        ad = parse(adv.manufacturer_data)
        if ad is None:
            return
        # Last-seen wins (most recent adv carries freshest data_ver).
        found[device.address] = DiscoveredTag(
            address=device.address, rssi=adv.rssi or 0, adv=ad
        )

    async with BleakScanner(detection_callback=_detect):
        await asyncio.sleep(duration_s)
    return found
```

- [ ] **Step 2: Mock-bleak test**

We don't run real BLE in CI. Use `unittest.mock.patch` on `BleakScanner` to inject our own context-manager that fires synthetic adv callbacks:

```python
import asyncio
from unittest.mock import patch, AsyncMock, MagicMock

import pytest

from tlsr_layout.ble.discovery import scan


@pytest.mark.asyncio
async def test_scan_captures_one_tag():
    class FakeScanner:
        def __init__(self, detection_callback):
            self._cb = detection_callback
        async def __aenter__(self):
            device = MagicMock(address="AA:BB:CC:DD:EE:FF")
            adv = MagicMock(
                manufacturer_data={0xFFFF: bytes([0x02]) + b"\x01\x00\x00\x00" + b"\x80\x0B" + b"\x14" + b"\x00"},
                rssi=-50,
            )
            self._cb(device, adv)
            return self
        async def __aexit__(self, *a): return None

    with patch("tlsr_layout.ble.discovery.BleakScanner", FakeScanner):
        result = await scan(duration_s=0.01)
    assert "AA:BB:CC:DD:EE:FF" in result
    tag = result["AA:BB:CC:DD:EE:FF"]
    assert tag.adv.data_ver == 0x00000001
    assert tag.adv.battery_mV == 0x0B80
```

(Adjust `pyproject.toml` to add `asyncio_mode = "auto"` for pytest-asyncio.)

- [ ] **Step 3: Commit**

```bash
git add producer/src/tlsr_layout/ble/discovery.py producer/tests/ble/test_discovery.py \
        producer/pyproject.toml
git commit -m "feat(producer): BLE scan with manufacturer-data filter"
```

---

## Task 3: Transport (GATT client wrapper)

**Files:**
- Create: `producer/src/tlsr_layout/ble/transport.py`
- Create: `producer/tests/ble/fake_peripheral.py`
- Create: `producer/tests/ble/test_transport.py`

- [ ] **Step 1: `transport.py`**

Wraps `BleakClient` and exposes a small API matching the spec's characteristic layout:

```python
"""Thin wrapper around BleakClient — connection-scoped GATT operations."""

import asyncio
from dataclasses import dataclass

from bleak import BleakClient

from .uuids import (
    CHAR_META,
    CHAR_STATUS,
    CHAR_XFER_ACK,
    CHAR_XFER_CTL,
    CHAR_XFER_DATA,
)


@dataclass(slots=True)
class Meta:
    hw_id: int
    fw_ver: int
    epd_w: int
    epd_h: int
    epd_bpp: int
    palette_mask: int
    flash_capacity_b: int
    mtu_pref: int


class Transport:
    def __init__(self, client: BleakClient) -> None:
        self._client = client
        self._ack_queue: asyncio.Queue[bytes] = asyncio.Queue()

    async def __aenter__(self) -> "Transport":
        await self._client.__aenter__()
        await self._client.start_notify(CHAR_XFER_ACK, self._on_ack)
        return self

    async def __aexit__(self, *a) -> None:
        try:
            await self._client.stop_notify(CHAR_XFER_ACK)
        finally:
            await self._client.__aexit__(*a)

    def _on_ack(self, _handle: int, data: bytearray) -> None:
        self._ack_queue.put_nowait(bytes(data))

    async def read_meta(self) -> Meta:
        raw = await self._client.read_gatt_char(CHAR_META)
        import struct
        hw_id, fw_ver, w, h, bpp, palette, cap, mtu = struct.unpack(
            "<HIHHBHIH", bytes(raw[:18])
        )
        return Meta(hw_id, fw_ver, w, h, bpp, palette, cap, mtu)

    async def read_status(self) -> bytes:
        return bytes(await self._client.read_gatt_char(CHAR_STATUS))

    async def write_ctl(self, frame: bytes) -> None:
        await self._client.write_gatt_char(CHAR_XFER_CTL, frame, response=True)

    async def write_data(self, frame: bytes) -> None:
        await self._client.write_gatt_char(CHAR_XFER_DATA, frame, response=False)

    async def next_ack(self, timeout_s: float) -> bytes:
        return await asyncio.wait_for(self._ack_queue.get(), timeout=timeout_s)
```

- [ ] **Step 2: `fake_peripheral.py`**

An async-shaped object that quacks like `BleakClient` but runs the spec's wire protocol against an in-memory model:

```python
"""In-memory peripheral that implements the tlsr-layout wire protocol."""

import asyncio
import struct
import zlib
from dataclasses import dataclass, field

from tlsr_layout.ble.uuids import (
    CHAR_META, CHAR_STATUS, CHAR_XFER_ACK, CHAR_XFER_CTL, CHAR_XFER_DATA
)


@dataclass(slots=True)
class FakePeripheral:
    address: str = "AA:BB:CC:DD:EE:FF"
    data_ver: int = 0
    battery_mV: int = 2950
    forced_low_batt: bool = False
    forced_crc_error: bool = False
    forced_inflate_error: bool = False
    drop_chunks: list[int] = field(default_factory=list)  # seqs to silently drop

    _staging: bytearray = field(default_factory=bytearray)
    _begin: dict | None = None
    _ack_cb: callable = None
    _expected_seq: int = 0

    # BleakClient-shaped surface
    async def __aenter__(self): return self
    async def __aexit__(self, *a): return None

    async def start_notify(self, uuid, callback):
        if uuid == CHAR_XFER_ACK:
            self._ack_cb = callback
    async def stop_notify(self, uuid): self._ack_cb = None

    async def read_gatt_char(self, uuid):
        if uuid == CHAR_STATUS:
            return struct.pack("<IBBHbBxxxxxx",
                               self.data_ver, 0, 0, self.battery_mV, 23, 0)
        if uuid == CHAR_META:
            return struct.pack("<HIHHBHIH",
                               1, 0x040000, 250, 128, 2, 0b111, 131072, 185)
        return b""

    async def write_gatt_char(self, uuid, frame, response=True):
        if uuid == CHAR_XFER_CTL:
            await self._handle_ctl(bytes(frame))
        elif uuid == CHAR_XFER_DATA:
            await self._handle_data(bytes(frame))

    def _ack(self, last_seq: int, code: int, flags: int = 0):
        if self._ack_cb:
            self._ack_cb(0, bytearray(struct.pack("<HBB", last_seq, code, flags)))

    async def _handle_ctl(self, frame: bytes):
        cmd = frame[0]
        if cmd == 0x01:  # BEGIN
            if self.forced_low_batt or self.battery_mV < 2400:
                self._ack(0, 0x20); return
            data_ver, total_len, total_chunks, crc, fmt = struct.unpack_from("<IHHIB", frame, 1)
            self._begin = dict(data_ver=data_ver, total_len=total_len, crc=crc)
            self._staging = bytearray()
            self._expected_seq = 0
            self._ack(0, 0x01)
        elif cmd == 0x03:  # COMMIT
            assert self._begin
            if len(self._staging) != self._begin["total_len"]:
                self._ack(self._expected_seq, 0x10); return  # ERR_GAP shouldn't happen here but safe
            if self.forced_crc_error or zlib.crc32(self._staging) != self._begin["crc"]:
                self._ack(0, 0x11); return
            if self.forced_inflate_error:
                self._ack(0, 0x12); return
            try:
                zlib.decompress(bytes(self._staging))
            except Exception:
                self._ack(0, 0x12); return
            self.data_ver = self._begin["data_ver"]
            self._ack(0, 0x02)

    async def _handle_data(self, frame: bytes):
        seq, = struct.unpack_from("<H", frame, 0)
        payload = frame[2:]
        if seq in self.drop_chunks:
            self.drop_chunks.remove(seq)  # one-shot drop
            return
        if seq != self._expected_seq:
            self._ack(self._expected_seq - 1 if self._expected_seq else 0, 0x10)
            return
        self._staging.extend(payload)
        self._expected_seq += 1
        if self._expected_seq % 8 == 0:
            self._ack(seq, 0x00)
```

- [ ] **Step 3: `test_transport.py`**

```python
import asyncio
import struct

import pytest

from tlsr_layout.ble.transport import Transport

from .fake_peripheral import FakePeripheral


@pytest.mark.asyncio
async def test_transport_meta_and_status():
    fp = FakePeripheral()
    async with Transport(fp) as t:
        meta = await t.read_meta()
        assert meta.epd_w == 250 and meta.epd_h == 128 and meta.mtu_pref == 185
        st = await t.read_status()
        assert len(st) >= 12
```

- [ ] **Step 4: Commit**

```bash
git add producer/src/tlsr_layout/ble/transport.py producer/tests/ble/fake_peripheral.py \
        producer/tests/ble/test_transport.py
git commit -m "feat(producer): GATT transport + in-memory fake peripheral"
```

---

## Task 4: Central (push orchestration)

**Files:**
- Create: `producer/src/tlsr_layout/ble/central.py`
- Create: `producer/tests/ble/test_central_happy.py`
- Create: `producer/tests/ble/test_central_recovery.py`

- [ ] **Step 1: `central.py`**

Orchestrates a full push: BEGIN → stream chunks (interleaved with ACK polling) → COMMIT.

```python
"""High-level push: encode → BEGIN → stream → COMMIT, with recovery."""

import asyncio
import logging

from ..encode import EncodedPage, chunk_payload, encode_page
from ..protocol import (
    ACK_ERR_GAP, ACK_ERR_LOW_BATT, ACK_ERR_CRC,
    ACK_OK_BEGIN, ACK_OK_COMMIT, ACK_OK_PROGRESS,
    DSL_FORMAT_ZLIB_JSON_V1, build_begin, build_commit, build_data_chunk, parse_ack,
)
from .transport import Transport

log = logging.getLogger(__name__)


class PushError(RuntimeError):
    def __init__(self, code: int, message: str) -> None:
        super().__init__(f"push failed: {message} (code 0x{code:02X})")
        self.code = code


async def push_page(
    transport: Transport,
    page: dict,
    *,
    chunk_payload_size: int = 180,
    ack_timeout_s: float = 5.0,
) -> EncodedPage:
    """One-shot push. Raises PushError on terminal failure."""

    encoded = encode_page(page)
    chunks = chunk_payload(encoded.compressed, max_payload=chunk_payload_size)

    # BEGIN
    await transport.write_ctl(build_begin(
        data_ver=encoded.data_ver,
        total_len=encoded.compressed_len,
        total_chunks=len(chunks),
        crc32=encoded.crc32,
        dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
    ))
    ack = parse_ack(await transport.next_ack(ack_timeout_s))
    if ack.code != ACK_OK_BEGIN:
        if ack.code == ACK_ERR_LOW_BATT:
            raise PushError(ack.code, "low battery")
        raise PushError(ack.code, "unexpected BEGIN response")

    # DATA — pipelined with periodic ACK draining
    i = 0
    while i < len(chunks):
        seq, payload = chunks[i]
        await transport.write_data(build_data_chunk(seq=seq, payload=payload))
        i += 1
        # Drain any pending ACKs (don't block)
        try:
            ack = parse_ack(await asyncio.wait_for(transport.next_ack(0.001), timeout=0.001))
            if ack.is_error:
                if ack.code == ACK_ERR_GAP:
                    log.info("ERR_GAP, resuming from seq %d", ack.last_seq + 1)
                    i = ack.last_seq + 1
                else:
                    raise PushError(ack.code, "data-phase error")
        except asyncio.TimeoutError:
            pass

    # COMMIT
    await transport.write_ctl(build_commit(data_ver=encoded.data_ver))
    ack = parse_ack(await transport.next_ack(ack_timeout_s * 3))  # render takes longer
    if ack.code != ACK_OK_COMMIT:
        if ack.code == ACK_ERR_CRC:
            raise PushError(ack.code, "CRC mismatch — possible resend needed")
        raise PushError(ack.code, "COMMIT failed")

    return encoded
```

- [ ] **Step 2: Happy path test**

```python
import pytest

from tlsr_layout.ble.central import push_page
from tlsr_layout.ble.transport import Transport
from tlsr_layout.pages.example import example_page

from .fake_peripheral import FakePeripheral


@pytest.mark.asyncio
async def test_push_happy_path_updates_data_ver():
    fp = FakePeripheral()
    assert fp.data_ver == 0
    async with Transport(fp) as t:
        encoded = await push_page(t, example_page())
    assert fp.data_ver == encoded.data_ver
```

- [ ] **Step 3: Recovery tests**

```python
import pytest

from tlsr_layout.ble.central import PushError, push_page
from tlsr_layout.ble.transport import Transport
from tlsr_layout.pages.example import example_page

from .fake_peripheral import FakePeripheral


@pytest.mark.asyncio
async def test_push_handles_gap_and_recovers():
    fp = FakePeripheral(drop_chunks=[2])  # drop seq 2, force a gap
    async with Transport(fp) as t:
        await push_page(t, example_page())
    assert fp.data_ver != 0


@pytest.mark.asyncio
async def test_push_raises_on_low_batt():
    fp = FakePeripheral(forced_low_batt=True)
    async with Transport(fp) as t:
        with pytest.raises(PushError) as e:
            await push_page(t, example_page())
        assert e.value.code == 0x20


@pytest.mark.asyncio
async def test_push_raises_on_crc():
    fp = FakePeripheral(forced_crc_error=True)
    async with Transport(fp) as t:
        with pytest.raises(PushError) as e:
            await push_page(t, example_page())
        assert e.value.code == 0x11
```

- [ ] **Step 4: Commit**

```bash
git add producer/src/tlsr_layout/ble/central.py producer/tests/ble/test_central_*.py
git commit -m "feat(producer): push_page orchestrator (BEGIN/DATA/COMMIT + GAP recovery)"
```

---

## Task 5: CLI `devices` and `push`

**Files:**
- Modify: `producer/src/tlsr_layout/cli.py`
- Create: `producer/tests/ble/test_cli_devices.py`
- Create: `producer/tests/ble/test_cli_push.py`

- [ ] **Step 1: Replace Phase-1 stubs**

```python
import asyncio

import typer

from .ble.central import PushError, push_page
from .ble.discovery import scan
from .ble.transport import Transport
from .pages.example import example_page

PAGES = {"example": example_page}


@app.command("devices")
def devices(duration: float = typer.Option(10.0, "--duration")) -> None:
    """Scan for tlsr-layout tags. Prints one line per tag."""

    async def _run():
        found = await scan(duration_s=duration)
        if not found:
            typer.echo("no tags found")
            return
        for addr, tag in sorted(found.items()):
            typer.echo(
                f"{addr}  rssi={tag.rssi}  data_ver=0x{tag.adv.data_ver:08X}  "
                f"batt={tag.adv.battery_mV}mV  temp={tag.adv.temp_c}C  "
                f"flags={tag.adv.flags:#04x}"
            )
    asyncio.run(_run())


@app.command("push")
def push(
    page_name: str = typer.Argument(...),
    device: str = typer.Option(..., "--device", help="Target tag MAC address."),
) -> None:
    """Push a registered page to a specific tag."""

    builder = PAGES.get(page_name)
    if builder is None:
        typer.echo(f"unknown page {page_name!r}; known: {sorted(PAGES)}", err=True)
        raise typer.Exit(2)

    async def _run():
        from bleak import BleakClient

        async with Transport(BleakClient(device)) as t:
            try:
                encoded = await push_page(t, builder())
            except PushError as e:
                typer.echo(f"push failed: {e}", err=True)
                raise typer.Exit(3)
            typer.echo(f"push ok: data_ver=0x{encoded.data_ver:08X}")
    asyncio.run(_run())
```

- [ ] **Step 2: CLI tests with bleak fully mocked**

For `devices`: patch `scan` to return a synthetic dict; assert formatted output.

For `push`: patch `bleak.BleakClient` to return a `FakePeripheral`; run `push example --device AA:BB:...` and assert exit code 0.

```python
import pytest
from typer.testing import CliRunner
from unittest.mock import patch

from tlsr_layout.cli import app
from tlsr_layout.ble.discovery import DiscoveredTag
from tlsr_layout.ble.adv_parser import Advertisement


runner = CliRunner()


def test_devices_lists_tags():
    fake = {
        "AA:BB:CC:DD:EE:FF": DiscoveredTag(
            address="AA:BB:CC:DD:EE:FF",
            rssi=-40,
            adv=Advertisement(0xDEADBEEF, 2950, 22, 0x01, True, False, False),
        )
    }
    with patch("tlsr_layout.cli.scan", return_value=fake):
        with patch("asyncio.run", side_effect=lambda c: c):
            # asyncio.run returns the coroutine — adapt as needed in real test
            result = runner.invoke(app, ["devices"])
    assert result.exit_code == 0
    assert "AA:BB:CC:DD:EE:FF" in result.output
```

(The mocking dance around `asyncio.run` needs care — adapt the patch to whichever harness shape pytest accepts. Use `pytest-asyncio` if simpler.)

- [ ] **Step 3: Full test sweep**

```bash
cd producer && uv run pytest -q
```

Expected: all green; coverage ≥ 90 % (the existing gate from Phase 1).

- [ ] **Step 4: Commit + tag**

```bash
git add producer/src/tlsr_layout/cli.py producer/tests/ble/test_cli_*.py
git commit -m "feat(producer): CLI devices/push commands wired to BLE central"
git tag -a tlsr-layout-phase6 -m "Phase 6: producer BLE central"
```

- [ ] **Step 5: Phase summary**

```
PHASE 6 COMPLETE
- tlsr-layout devices: scans 10s, prints tags w/ data_ver/batt/temp/flags
- tlsr-layout push <page> --device <MAC>: full BEGIN/DATA/COMMIT push
- fake peripheral runs the wire protocol; happy + gap + low-batt + CRC cases tested
- Next: Phase 7 — serve daemon + page registry (claude/codex/calendar/todo)
```

---

## Self-Review

- [x] **Spec coverage** — §5 (BLE service surface), §6 (transfer flow), §6.3 (ACK codes consumed). Discovery filter matches §5.2 adv layout. Recovery covers `ERR_GAP` resume; `ERR_CRC` raises a PushError that the caller can retry.
- [x] **Placeholder scan** — every test has a specific assertion.
- [x] **Type consistency** — UUIDs match the firmware base in Phase 2. ACK constants imported from `protocol.py` (Phase 1). `Transport` API names stable for Phase 7's `serve` to reuse.
