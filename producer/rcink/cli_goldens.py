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

from rcink.devices_profile import C3_X4, EPD397, RLCD, S3, T_AMOLED, TLSR, DeviceProfile
from rcink.preview_png import render_to_png, unsupported_node_type

GOLDEN_ROOT = Path(__file__).resolve().parent.parent.parent / "dsl" / "golden"
DEVICES: tuple[DeviceProfile, ...] = (S3, C3_X4, TLSR, T_AMOLED, EPD397, RLCD)


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


def _build_claude_compact() -> dict:
    from rcink.pages.claude import build_claude_compact_page
    from rcink.sources.claude import ClaudeUsage

    usage = ClaudeUsage(
        five_hour_pct=42.0,
        seven_day_pct=15.0,
        opus_pct=None,
        sonnet_pct=None,
        five_hour_reset="2h 14m",
        seven_day_reset="5d 4h",
        plan="Max 20x",
    )
    return build_claude_compact_page(usage)


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
    from rcink.sources.weather import Weather

    return build_overview_page(
        claude=None,
        codex=None,
        calendar=None,
        reminders=None,
        weather=Weather(
            temp=28.4,
            high=31.2,
            low=24.6,
            unit="C",
            code=80,
            precip_probability=67,
            is_day=True,
            observed_at=_NOW,
        ),
        github=None,
        quote=None,
        now=_NOW,
    )


def _build_calendar() -> dict:
    from rcink.pages.calendar import build_calendar_page
    from rcink.sources.calendar import CalEvent, TodaysCalendar

    events = [
        CalEvent(
            start=_NOW + dt.timedelta(minutes=30),
            end=_NOW + dt.timedelta(minutes=75),
            title="Design review",
            calendar="Work",
            is_all_day=False,
        ),
        CalEvent(
            start=_NOW + dt.timedelta(hours=3),
            end=_NOW + dt.timedelta(hours=4),
            title="Firmware sync",
            calendar="Work",
            is_all_day=False,
        ),
        CalEvent(
            start=dt.datetime(2026, 1, 1, 0, 0, tzinfo=_NOW.tzinfo),
            end=dt.datetime(2026, 1, 2, 0, 0, tzinfo=_NOW.tzinfo),
            title="New Year",
            calendar="Holidays",
            is_all_day=True,
        ),
    ]
    upcoming = [
        CalEvent(
            start=_NOW + dt.timedelta(days=1, hours=2),
            end=_NOW + dt.timedelta(days=1, hours=3),
            title="Coffee with Ada",
            calendar="Personal",
            is_all_day=False,
        )
    ]
    return build_calendar_page(TodaysCalendar(today=_NOW.date(), events=events, upcoming=upcoming), now=_NOW)


def _build_github() -> dict:
    from rcink.pages.github import build_github_page
    from rcink.sources.github import GRID_CELLS, GitHubActivity, LangWeek, RepoWeek

    daily_counts = [((i * 7) + (i // 5)) % 11 for i in range(GRID_CELLS)]
    return build_github_page(
        GitHubActivity(
            user="rocry",
            today_contributions=daily_counts[-1],
            week_contributions=sum(daily_counts[-7:]),
            streak=8,
            total_14w=sum(daily_counts),
            total_year=1842,
            daily_counts=daily_counts,
            repos_week=[
                RepoWeek(name="rcink", commits=12),
                RepoWeek(name="carevie", commits=7),
                RepoWeek(name="siu", commits=4),
            ],
            langs_week=[
                LangWeek(name="Python", pct=42),
                LangWeek(name="C++", pct=31),
                LangWeek(name="Go", pct=27),
            ],
        ),
        now=_NOW,
    )


def _build_todo() -> dict:
    from rcink.pages.todo import build_todo_page
    from rcink.sources.reminders import Reminder, TodaysReminders

    items = [
        Reminder(title="Review rcink golden output", due=None, updated=_NOW, priority=1),
        Reminder(
            title="https://github.com/rocrp/rcink/pulls/42",
            due=_NOW + dt.timedelta(hours=2),
            updated=_NOW - dt.timedelta(minutes=40),
            priority=4,
        ),
        Reminder(title="Order replacement flex cable", due=None, updated=_NOW - dt.timedelta(hours=3), priority=0),
        Reminder(title="Check battery current after sleep", due=None, updated=_NOW - dt.timedelta(days=1), priority=7),
    ]
    return build_todo_page(TodaysReminders(list_name="Inbox", items=items, incomplete_total=9), now=_NOW)


_BUILDERS: dict[str, Callable[[], dict]] = {
    "quote": _build_quote,
    "image": _build_image,
    "claude": _build_claude,
    "claude_compact": _build_claude_compact,
    "codex": _build_codex,
    "overview": _build_overview,
    "calendar": _build_calendar,
    "github": _build_github,
    "todo": _build_todo,
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
