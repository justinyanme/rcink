"""Pick today's quote and push one page event to Axiom."""

from __future__ import annotations

import datetime as dt

import typer
from loguru import logger
from rcpaper.axiom import ingest
from rcpaper.pages.quote import build_quote_page
from rcpaper.sources.quotes import pick_quote

app = typer.Typer(add_completion=False, help=__doc__)


@app.command()
def run() -> None:
    now = dt.datetime.now()
    quote = pick_quote(now.date())
    logger.info(f"quote: {' / '.join(quote.lines)} {quote.attrib}")

    page = build_quote_page(quote, now=now)
    # Quote sits at the tail of the page rotation, after Claude (10) and Codex (20).
    ingest(id="quote", layout=page, order=80)


if __name__ == "__main__":
    app()
