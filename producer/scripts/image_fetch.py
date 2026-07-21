"""Push a remote-image page to Axiom."""

from __future__ import annotations

from urllib.parse import urlparse

import typer
from loguru import logger

from rcink.axiom import ingest
from rcink.pages.image import build_image_page

app = typer.Typer(add_completion=False, help=__doc__)


def _infer_format(url: str) -> str | None:
    path = urlparse(url).path.casefold()
    if path.endswith(".png"):
        return "png"
    if path.endswith(".jpg") or path.endswith(".jpeg"):
        return "jpg"
    if path.endswith(".bmp"):
        return "bmp"
    if path.endswith(".qoi"):
        return "qoi"
    return None


@app.command()
def run(
    url: str = typer.Argument(..., help="Remote image URL the device should download and render."),
    page_id: str = typer.Option("image", "--id", help="Page id stored in Axiom."),
    title: str = typer.Option("IMAGE", "--title", help="Header label shown above the image."),
    caption: str | None = typer.Option(None, "--caption", help="Optional footer caption. Defaults to hostname."),
    format: str | None = typer.Option(
        None, "--format", help="png|jpg|bmp|qoi. Auto-inferred from the URL when omitted."
    ),
    order: int = typer.Option(60, "--order", help="Rotation order. Lower shows earlier."),
) -> None:
    image_format = format or _infer_format(url)
    if image_format is None:
        raise typer.BadParameter("could not infer image format from URL; pass --format png|jpg|bmp|qoi")
    page = build_image_page(url=url, title=title, caption=caption, format=image_format)
    ingest(id=page_id, layout=page, order=order)
    logger.info(f"image page pushed: id={page_id!r} format={image_format} url={url}")


if __name__ == "__main__":
    app()
