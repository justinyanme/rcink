"""Render a producer page builder to an HTML preview and open it in the browser.

Usage:
    uv run preview claude         # build via rcink.sources.claude + pages.claude
    uv run preview codex          # build via rcink.sources.codex + pages.codex
    uv run preview github -d c3-x4 # render with a non-default device profile
    uv run preview --file f.json  # render an arbitrary layout dict from disk
    uv run preview --axiom claude # pull the latest page event from Axiom

No device flash, no Axiom round-trip in the default path — runs the same
producer code the launchd job runs, so you can edit the page builders and
iterate on layout instantly.
"""

from __future__ import annotations

import datetime as dt
import json
import os
import webbrowser
from pathlib import Path
from typing import Any

import typer
from loguru import logger
from rcink.devices_profile import get_profile
from rcink.preview import to_html

app = typer.Typer(add_completion=False, help=__doc__, no_args_is_help=True)
DEFAULT_PREVIEW_OUT = Path("/tmp/rcpaper-preview.html")
FILE_OPTION = typer.Option(None, "--file", help="Render a layout dict from this JSON file.")
AXIOM_OPTION = typer.Option(False, "--axiom", help="Pull the latest layout for <page> from Axiom.")
OUT_OPTION = typer.Option(DEFAULT_PREVIEW_OUT, "--out", help="Output HTML path.")
NO_OPEN_OPTION = typer.Option(False, "--no-open", help="Don't open the browser.")
DEVICE_OPTION = typer.Option("s3", "--device", "-d", help="Device profile for viewport, fonts, and palette.")


def _build_local(page_id: str) -> dict[str, Any]:
    if page_id == "claude":
        from rcink.pages.claude import build_claude_page
        from rcink.sources.claude import fetch_claude_usage

        return build_claude_page(fetch_claude_usage(), sparkline=None)
    if page_id == "codex":
        from rcink.pages.codex import build_codex_page
        from rcink.sources.codex import fetch_codex_usage

        return build_codex_page(fetch_codex_usage(), sparkline=None)
    if page_id == "calendar":
        from rcink.pages.calendar import build_calendar_page
        from rcink.sources.calendar import fetch_todays_events

        now = dt.datetime.now().astimezone()
        return build_calendar_page(fetch_todays_events(now=now), now=now)
    if page_id == "todo":
        from rcink.pages.todo import build_todo_page
        from rcink.sources.reminders import fetch_todays_reminders

        now = dt.datetime.now().astimezone()
        return build_todo_page(fetch_todays_reminders(now=now), now=now)
    if page_id == "quote":
        from rcink.pages.quote import build_quote_page
        from rcink.sources.quotes import pick_quote

        now = dt.datetime.now()
        return build_quote_page(pick_quote(now.date()), now=now)
    if page_id == "github":
        from rcink.pages.github import build_github_page
        from rcink.sources.github import fetch_github_activity

        return build_github_page(fetch_github_activity(), now=dt.datetime.now())
    raise ValueError(f"unknown page id: {page_id} (expected 'claude'/'codex'/'calendar'/'todo'/'quote'/'github')")


def _pull_axiom(page_id: str) -> dict[str, Any]:
    from rcink.axiom import query_apl

    rows = query_apl(
        f"['{os.environ.get('AXIOM_DATASET', 'rocry')}'] "
        f"| where ['type'] == 'rcpaper_page' and ['id'] == '{page_id}' "
        "| sort by _time desc | limit 1 | project ['layout_json']",
        lookback=dt.timedelta(hours=1),
    )
    if not rows:
        raise SystemExit(f"no '{page_id}' page in Axiom in the last hour")
    return json.loads(rows[0]["layout_json"])


@app.command()
def main(
    page: str = typer.Argument(None, help="Page id: 'claude' or 'codex' (omit if using --file)."),
    file: Path | None = FILE_OPTION,
    axiom: bool = AXIOM_OPTION,
    out: Path = OUT_OPTION,
    no_open: bool = NO_OPEN_OPTION,
    device: str = DEVICE_OPTION,
) -> None:
    prof = get_profile(device)
    if file:
        layout = json.loads(file.read_text())
        title = f"file:{file.name}"
    elif axiom:
        if not page:
            raise SystemExit("--axiom requires a page id")
        layout = _pull_axiom(page)
        title = f"axiom:{page}"
    else:
        if not page:
            raise SystemExit("page id is required (or pass --file)")
        layout = _build_local(page)
        title = f"local:{page}"

    html = to_html(layout, title=title, prof=prof)
    out.write_text(html)
    logger.info(f"wrote {out}")
    if not no_open:
        webbrowser.open(f"file://{out}")


if __name__ == "__main__":
    app()
