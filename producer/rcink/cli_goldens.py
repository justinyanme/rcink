"""Regenerate dsl/golden/<device>/<page>.png from current page builders.

Run with `uv run rcink-goldens regen` from producer/. Writes/overwrites PNGs
into ../dsl/golden/<device>/.

Each page entry uses frozen fixtures so the output is deterministic and
matches a static snapshot. To deliberately update goldens after a producer
change, run regen and commit the resulting PNGs.
"""

from __future__ import annotations

import datetime as dt
from collections.abc import Callable
from pathlib import Path
from typing import Annotated

import typer

from rcink.devices_profile import C3_X4, S3, T_AMOLED, TLSR, DeviceProfile
from rcink.preview_png import render_to_png, unsupported_node_type

GOLDEN_ROOT = Path(__file__).resolve().parent.parent.parent / "dsl" / "golden"
DEVICES: tuple[DeviceProfile, ...] = (S3, C3_X4, TLSR, T_AMOLED)


# ---- frozen fixtures + builder invocations ------------------------------

_NOW = dt.datetime(2026, 1, 1, 12, 0, 0)


def _build_quote() -> dict:
    from rcink.pages.quote import build_quote_page
    from rcink.sources.quotes import pick_quote

    q = pick_quote(_NOW.date())
    return build_quote_page(q, now=_NOW)


def _build_image() -> dict:
    from rcink.pages.image import build_image_page

    return build_image_page(
        url="https://example.invalid/demo.png",
        title="DEMO IMAGE",
        caption="rcink golden fixture",
        width=400,
        height=300,
    )


def _build_claude() -> dict:
    from rcink.pages.claude import build_claude_page
    from rcink.sources.claude import ClaudeUsage

    usage = ClaudeUsage(
        five_hour_pct=42.0,
        seven_day_pct=15.0,
        opus_pct=8.0,
        sonnet_pct=70.0,
        five_hour_reset="2h 14m",
        seven_day_reset="5d 4h",
        plan="Max 20x",
    )
    return build_claude_page(usage, sparkline=[5, 12, 18, 14, 22, 19, 28, 31])


def _build_codex() -> dict:
    from rcink.pages.codex import build_codex_page
    from rcink.sources.codex import CodexUsage

    usage = CodexUsage(
        primary_pct=30.0,
        primary_reset="4h 50m",
        primary_window_min=300,
        secondary_pct=18.0,
        secondary_reset="6d 23h",
        secondary_window_min=10080,
        credits_dollars=12.34,
        plan="Plus",
    )
    return build_codex_page(usage, sparkline=[8, 14, 11, 19, 22, 18, 25, 27])


def _build_overview() -> dict:
    from rcink.pages.overview import build_overview_page

    return build_overview_page(
        claude=None,
        codex=None,
        calendar=None,
        reminders=None,
        github=None,
        quote=None,
        now=_NOW,
    )


_BUILDERS: dict[str, Callable[[], dict]] = {
    "quote": _build_quote,
    "image": _build_image,
    "claude": _build_claude,
    "codex": _build_codex,
    "overview": _build_overview,
}


# ---- CLI ---------------------------------------------------------------

app = typer.Typer(add_completion=False, help="Regenerate per-device golden PNGs.", no_args_is_help=True)


@app.callback()
def _main() -> None:
    pass


def _layout_of(result: dict) -> dict:
    """Page builders may return either {layout: {...}} or the layout root directly."""
    return result.get("layout", result)


@app.command()
def regen(
    pages: Annotated[list[str] | None, typer.Option("--page", "-p", help="Only these page ids; default = all")] = None,
    devices: Annotated[
        list[str] | None, typer.Option("--device", "-d", help="Only these device profiles; default = all")
    ] = None,
) -> None:
    selected_pages = pages or list(_BUILDERS.keys())
    selected_devices = [next(d for d in DEVICES if d.name == name) for name in devices] if devices else list(DEVICES)

    for page in selected_pages:
        layout = _layout_of(_BUILDERS[page]())
        for prof in selected_devices:
            unsupported = unsupported_node_type(layout, prof)
            if unsupported is not None:
                typer.echo(f"skipped {prof.name}/{page}: unsupported {unsupported}")
                continue
            out_dir = GOLDEN_ROOT / prof.name
            out_dir.mkdir(parents=True, exist_ok=True)
            out_path = out_dir / f"{page}.png"
            img = render_to_png(layout, prof)
            img.save(out_path)
            typer.echo(f"wrote {out_path.relative_to(GOLDEN_ROOT.parent.parent)}")


def main() -> None:
    app()
