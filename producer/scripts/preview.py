"""Render a producer page builder to an HTML preview and open it in the browser.

Usage:
    uv run preview claude         # build via rcpaper.sources.claude + pages.claude
    uv run preview codex          # build via rcpaper.sources.codex + pages.codex
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
from rcpaper.preview import to_html

app = typer.Typer(add_completion=False, help=__doc__, no_args_is_help=True)


def _build_local(page_id: str) -> dict[str, Any]:
    if page_id == "claude":
        from rcpaper.pages.claude import build_claude_page
        from rcpaper.sources.claude import fetch_claude_usage

        usage = fetch_claude_usage()
        return build_claude_page(usage, sparkline=None, updated_at=dt.datetime.now())
    if page_id == "codex":
        from rcpaper.pages.codex import build_codex_page
        from rcpaper.sources.codex import fetch_codex_usage

        usage = fetch_codex_usage()
        return build_codex_page(usage, sparkline=None, updated_at=dt.datetime.now())
    raise ValueError(f"unknown page id: {page_id} (expected 'claude' or 'codex')")


def _pull_axiom(page_id: str) -> dict[str, Any]:
    from rcpaper.axiom import query_apl

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
    file: Path | None = typer.Option(None, "--file", help="Render a layout dict from this JSON file."),
    axiom: bool = typer.Option(False, "--axiom", help="Pull the latest layout for <page> from Axiom."),
    out: Path = typer.Option(Path("/tmp/rcpaper-preview.html"), "--out", help="Output HTML path."),
    no_open: bool = typer.Option(False, "--no-open", help="Don't open the browser."),
) -> None:
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

    html = to_html(layout, title=title)
    out.write_text(html)
    logger.info(f"wrote {out}")
    if not no_open:
        webbrowser.open(f"file://{out}")


if __name__ == "__main__":
    app()
