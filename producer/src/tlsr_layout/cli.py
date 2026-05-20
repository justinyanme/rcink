"""Command-line interface for the tlsr-layout producer."""

import asyncio
import json
import logging
import struct
from pathlib import Path
from typing import Annotated

import typer

from .ble.central import PushError, push_page
from .ble.discovery import scan
from .ble.transport import Transport
from .encode import chunk_payload, encode_page
from .pages.registry import build_page, page_names
from .protocol import DSL_FORMAT_ZLIB_JSON_V1, build_begin, build_data_chunk
from .serve import ServeConfig
from .serve import serve as serve_daemon

app = typer.Typer(no_args_is_help=True, add_completion=False)

# Phase 1 uses the conservative MTU; firmware will negotiate up later.
DEFAULT_CHUNK_PAYLOAD = 180


@app.command("dry-run")
def dry_run(
    page_name: Annotated[
        str, typer.Argument(help="Registered page name (see `tlsr-layout devices`).")
    ],
    out: Annotated[Path, typer.Option("--out", help="Output directory.")] = Path("./out"),
    chunk_payload_size: Annotated[
        int, typer.Option("--chunk", help="Bytes per XFER_DATA chunk.")
    ] = DEFAULT_CHUNK_PAYLOAD,
) -> None:
    """Build a page, encode it, write JSON + compressed bytes + chunk frames to disk."""

    try:
        page = build_page(page_name)
    except KeyError as exc:
        typer.echo(f"unknown page {page_name!r}; known: {page_names()}", err=True)
        raise typer.Exit(code=2) from exc

    out.mkdir(parents=True, exist_ok=True)
    encoded = encode_page(page)

    (out / f"{page_name}.json").write_bytes(encoded.canonical_json)
    (out / f"{page_name}.zlib").write_bytes(encoded.compressed)

    chunks = chunk_payload(encoded.compressed, max_payload=chunk_payload_size)

    with (out / f"{page_name}.chunks.bin").open("wb") as fh:
        # BEGIN frame, then each XFER_DATA chunk in order, length-prefixed for offline replay.
        begin = build_begin(
            data_ver=encoded.data_ver,
            total_len=encoded.compressed_len,
            total_chunks=len(chunks),
            crc32=encoded.crc32,
            dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
        )
        fh.write(struct.pack("<H", len(begin)))
        fh.write(begin)
        for seq, payload in chunks:
            frame = build_data_chunk(seq=seq, payload=payload)
            fh.write(struct.pack("<H", len(frame)))
            fh.write(frame)

    summary = {
        "page": page_name,
        "data_ver": encoded.data_ver,
        "uncompressed_len": encoded.uncompressed_len,
        "compressed_len": encoded.compressed_len,
        "crc32": encoded.crc32,
        "chunks": len(chunks),
        "chunk_payload_size": chunk_payload_size,
    }
    (out / f"{page_name}.summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    typer.echo(json.dumps(summary, indent=2))


@app.command("devices")
def devices(
    duration: Annotated[
        float, typer.Option("--duration", help="BLE scan duration in seconds.")
    ] = 10.0,
) -> None:
    """Scan for tlsr-layout tags."""

    async def _run() -> None:
        found = await scan(duration_s=duration)
        if not found:
            typer.echo("no tags found")
            return
        for address, tag in sorted(found.items()):
            typer.echo(
                f"{address}  rssi={tag.rssi}  data_ver=0x{tag.adv.data_ver:08X}  "
                f"batt={tag.adv.battery_mV}mV  temp={tag.adv.temp_c}C  "
                f"flags={tag.adv.flags:#04x}"
            )

    asyncio.run(_run())


@app.command("push")
def push(
    page_name: Annotated[str, typer.Argument()],
    device: Annotated[str, typer.Option("--device", help="Target tag MAC address.")],
) -> None:
    """Push a registered page to a tag."""

    try:
        page = build_page(page_name)
    except KeyError as exc:
        typer.echo(f"unknown page {page_name!r}; known: {page_names()}", err=True)
        raise typer.Exit(code=2) from exc

    async def _run() -> None:
        from bleak import BleakClient

        async with Transport(BleakClient(device)) as transport:
            try:
                encoded = await push_page(transport, page)
            except PushError as exc:
                typer.echo(f"push failed: {exc}", err=True)
                raise typer.Exit(code=3) from exc
            typer.echo(f"push ok: data_ver=0x{encoded.data_ver:08X}")

    asyncio.run(_run())


@app.command("serve")
def serve_cmd(
    device_filter: Annotated[str | None, typer.Option("--device-filter")] = None,
    rotation: Annotated[
        list[str] | None,
        typer.Option("--page", help="Repeatable. Default: all registered pages."),
    ] = None,
    log_level: Annotated[str, typer.Option("--log-level")] = "INFO",
) -> None:
    """Run the long-lived scan + push daemon."""

    logging.basicConfig(
        level=getattr(logging, log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    config = ServeConfig(device_filter=device_filter, rotation=rotation)
    asyncio.run(serve_daemon(config))
