"""Run every rcpaper page fetcher in one process; degrade on individual failure.

Used by the consolidated launchd job (`com.rocry.rcpaper.all`) so the user
only has one StartInterval timer to manage instead of one per page. A single
fetcher raising (expired CLI auth, Calendar permissions revoked, etc.) is
logged and skipped so the rest of the page rotation stays fresh.
"""

from __future__ import annotations

import sys
from collections.abc import Callable

import typer
from loguru import logger

from scripts.calendar_fetch import run as run_calendar
from scripts.claude_fetch import run as run_claude
from scripts.codex_fetch import run as run_codex
from scripts.overview_fetch import run as run_overview
from scripts.quote_fetch import run as run_quote
from scripts.todo_fetch import run as run_todo

app = typer.Typer(add_completion=False, help=__doc__)

# Ordered to match the on-device page rotation (overview=5, claude=10, codex=20,
# calendar=30, todo=40, quote=80). Order doesn't affect correctness — each
# fetcher writes an independent Axiom event — it just keeps the log readable.
FETCHERS: list[tuple[str, Callable[[], None]]] = [
    ("claude", run_claude),
    ("codex", run_codex),
    ("calendar", run_calendar),
    ("todo", run_todo),
    ("overview", run_overview),
    ("quote", run_quote),
]


@app.command()
def run() -> None:
    failed: list[str] = []
    for name, fn in FETCHERS:
        try:
            fn()
        except Exception as e:  # noqa: BLE001 — log + degrade, mirror overview_fetch
            logger.opt(exception=True).error(f"{name}: {e}")
            failed.append(name)
    if failed:
        logger.error(f"{len(failed)}/{len(FETCHERS)} failed: {', '.join(failed)}")
        sys.exit(1)
    logger.info(f"fetch-all: {len(FETCHERS)} ok")


if __name__ == "__main__":
    app()
