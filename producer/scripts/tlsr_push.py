"""Build a page and push it to the TLSR tag over BLE, with content-addressed dedup.

Examples:
    uv run tlsr-push claude            # show Claude usage on the tag
    uv run tlsr-push quote
    uv run tlsr-push claude --force    # ignore dedup, always transfer
    uv run tlsr-push claude --address 07DE1DAE-...   # skip the discovery filter

Dedup: the tag advertises the data_ver of what it currently shows. If the freshly
built page hashes to the same value, this exits without even connecting (mirrors
OpenEPaperLink's "already have it" skip). Use --force to override.
"""

from __future__ import annotations

import asyncio
import datetime as dt

import typer
from loguru import logger
from rcink.pages.claude import build_claude_compact_page
from rcink.pages.quote import build_quote_page
from rcink.sources.claude import fetch_claude_usage
from rcink.sources.quotes import pick_quote
from rcink.transport.ble.central import push_page
from rcink.transport.ble.discovery import discover_tag
from rcink.transport.ble.transport import connect_transport
from rcink.transport.ble_encode import encode_page

app = typer.Typer(add_completion=False, help=__doc__)

# 250x128 TLSR panels need compact, few-node layouts (the full s3/x4 page builders
# overflow the firmware's fixed layout arena). codex/overview need their own
# compact builders before they can be added here.
PAGES = ("claude", "quote")


def _build_page(page: str) -> dict:
    """Map a page name to a freshly-fetched, panel-appropriate layout."""
    if page == "claude":
        return build_claude_compact_page(fetch_claude_usage())
    if page == "quote":
        now = dt.datetime.now().astimezone()
        return build_quote_page(pick_quote(now.date()), now=now)
    raise typer.BadParameter(f"unknown page {page!r}; choose: {', '.join(PAGES)}")


async def _push(page: str, *, address: str | None, force: bool, timeout_s: float) -> None:
    layout = _build_page(page)
    encoded = encode_page(layout)
    logger.info(f"{page}: data_ver=0x{encoded.data_ver:08X} ({encoded.compressed_len} B compressed)")

    tag = await discover_tag(timeout_s=timeout_s, address=address)
    if tag is None:
        raise SystemExit(
            "no tag found — is it advertising? It only advertises for ~5 min after a "
            "reset/power-cycle; reset it to reopen the window."
        )
    logger.info(f"tag {tag.address} adv data_ver=0x{tag.adv.data_ver:08X} batt={tag.adv.battery_mV}mV")

    if not force and tag.adv.data_ver == encoded.data_ver:
        logger.success("tag already shows this exact content — skipping (no connection made)")
        return

    async with connect_transport(tag.address, device=tag.device, timeout_s=30.0) as transport:
        await push_page(transport, layout, current_data_ver=None if force else tag.adv.data_ver)
    logger.success(f"pushed {page} (data_ver=0x{encoded.data_ver:08X})")


@app.command()
def run(
    page: str = typer.Argument("claude", help="page to show: claude | quote"),
    address: str | None = typer.Option(None, help="CoreBluetooth UUID / MAC to target directly"),
    force: bool = typer.Option(False, help="ignore dedup; always transfer + redraw"),
    timeout: float = typer.Option(20.0, help="discovery timeout in seconds"),
) -> None:
    asyncio.run(_push(page, address=address, force=force, timeout_s=timeout))


if __name__ == "__main__":
    app()
