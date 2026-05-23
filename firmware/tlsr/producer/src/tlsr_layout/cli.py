"""Command-line interface for the tlsr-layout producer."""

import asyncio
import json
import logging
import struct
from html import escape
from pathlib import Path
from typing import Annotated

import typer

from .ble.central import PushError, push_page
from .ble.discovery import scan
from .ble.transport import connect_transport
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


@app.command("visual-review")
def visual_review(
    expected: Annotated[Path, typer.Option("--expected", help="Host-rendered expected PNG.")],
    capture: Annotated[
        Path, typer.Option("--capture", help="Camera/photo capture of the physical tag.")
    ],
    out: Annotated[Path, typer.Option("--out", help="Output HTML review artifact.")] = Path(
        "/tmp/tlsr-layout-visual-review.html"
    ),
) -> None:
    """Build a local visual review page for the final e-ink pixel gate."""

    for label, path in (("expected", expected), ("capture", capture)):
        if not path.is_file():
            typer.echo(f"{label} image not found: {path}", err=True)
            raise typer.Exit(code=2)

    expected_uri = expected.resolve().as_uri()
    capture_uri = capture.resolve().as_uri()
    expected_path = escape(str(expected))
    capture_path = escape(str(capture))
    pass_text = (
        "Pass only if the physical tag matches the expected layout: "
        "red CLAUDE header, black progress bar, red sparkline, bottom updated 14:32."
    )
    html = f"""<!doctype html>
<meta charset="utf-8">
<title>tlsr-layout visual review</title>
<style>
body {{ font: 14px -apple-system, BlinkMacSystemFont, sans-serif; margin: 24px; }}
.grid {{ display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 24px; }}
img {{ max-width: 100%; border: 1px solid #ccc; image-rendering: pixelated; }}
code {{ background: #f4f4f4; padding: 2px 4px; }}
</style>
<h1>tlsr-layout visual review</h1>
<p>{pass_text}</p>
<div class="grid">
  <section><h2>Expected</h2><p><code>{expected_path}</code></p><img src="{expected_uri}"></section>
  <section><h2>Capture</h2><p><code>{capture_path}</code></p><img src="{capture_uri}"></section>
</div>
"""
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(html)
    typer.echo(f"visual review: {out}")
    typer.echo(
        "pass criteria: red CLAUDE header, black progress bar, red sparkline, bottom updated 14:32"
    )


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
    scan_duration: Annotated[
        float, typer.Option("--scan-duration", help="Seconds to scan before connecting.")
    ] = 10.0,
) -> None:
    """Push a registered page to a tag."""

    try:
        page = build_page(page_name)
    except KeyError as exc:
        typer.echo(f"unknown page {page_name!r}; known: {page_names()}", err=True)
        raise typer.Exit(code=2) from exc

    async def _run() -> None:
        found = await scan(duration_s=scan_duration)
        tag = found.get(device)
        if tag is None:
            typer.echo(f"device {device!r} not found during scan", err=True)
            raise typer.Exit(code=2)
        async with connect_transport(device, device=tag.device) as transport:
            try:
                encoded = await push_page(transport, page)
            except PushError as exc:
                typer.echo(f"push failed: {exc}", err=True)
                raise typer.Exit(code=3) from exc
            typer.echo(f"push ok: data_ver=0x{encoded.data_ver:08X}")

    asyncio.run(_run())


@app.command("hardware-e2e")
def hardware_e2e(
    page_name: Annotated[str, typer.Argument()] = "example",
    device: Annotated[str | None, typer.Option("--device", help="Target tag MAC address.")] = None,
    scan_duration: Annotated[
        float, typer.Option("--scan-duration", help="Seconds to scan before connecting.")
    ] = 30.0,
    post_scan_duration: Annotated[
        float, typer.Option("--post-scan-duration", help="Seconds to scan for the pushed data_ver.")
    ] = 20.0,
) -> None:
    """Run scan -> push -> STATUS -> post-push advertising verification."""

    try:
        page = build_page(page_name)
    except KeyError as exc:
        typer.echo(f"unknown page {page_name!r}; known: {page_names()}", err=True)
        raise typer.Exit(code=2) from exc

    async def _run() -> None:
        found = await scan(duration_s=scan_duration)
        if device is not None:
            selected = found.get(device)
            if selected is None:
                typer.echo(f"device {device!r} not found during scan", err=True)
                raise typer.Exit(code=2)
            address = device
        else:
            if not found:
                typer.echo("no tags found during scan", err=True)
                raise typer.Exit(code=2)
            address, selected = sorted(found.items())[0]

        typer.echo(
            f"before {address} data_ver=0x{selected.adv.data_ver:08X} "
            f"batt={selected.adv.battery_mV}mV flags={selected.adv.flags:#04x}"
        )
        async with connect_transport(address, device=selected.device) as transport:
            try:
                encoded = await push_page(transport, page)
            except PushError as exc:
                typer.echo(f"push failed: {exc}", err=True)
                raise typer.Exit(code=3) from exc
            status = await transport.read_status()

        status_data_ver = int.from_bytes(status[:4], "little")
        typer.echo(
            f"push ok data_ver=0x{encoded.data_ver:08X} status_data_ver=0x{status_data_ver:08X}"
        )
        if status_data_ver != encoded.data_ver:
            typer.echo("STATUS data_ver mismatch", err=True)
            raise typer.Exit(code=4)

        post_found = await scan(duration_s=post_scan_duration)
        post_tag = next(
            (
                tag
                for tag in post_found.values()
                if tag.adv.data_ver == encoded.data_ver
                and not tag.adv.render_err
                and not tag.adv.display_busy
            ),
            None,
        )
        if post_tag is None:
            observed = ", ".join(
                f"{addr}=0x{tag.adv.data_ver:08X}/flags={tag.adv.flags:#04x}"
                for addr, tag in sorted(post_found.items())
            )
            typer.echo(
                f"post-push advertisement data_ver=0x{encoded.data_ver:08X} "
                "with display idle and no render error not found"
                + (f"; observed {observed}" if observed else ""),
                err=True,
            )
            raise typer.Exit(code=5)

        typer.echo(
            f"after {post_tag.address} data_ver=0x{post_tag.adv.data_ver:08X} "
            f"batt={post_tag.adv.battery_mV}mV flags={post_tag.adv.flags:#04x}"
        )
        typer.echo(f"hardware e2e ok: data_ver=0x{encoded.data_ver:08X}")

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
