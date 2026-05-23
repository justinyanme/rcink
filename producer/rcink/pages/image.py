"""Build a remote-image page for the PaperColor device."""

from __future__ import annotations

from urllib.parse import urlparse

from rcink.layout import image, spacer, text, vstack

IMAGE_WIDTH = 368
IMAGE_HEIGHT = 460


def _host_label(url: str) -> str:
    host = urlparse(url).hostname or url
    return host[4:] if host.startswith("www.") else host


def build_image_page(
    *,
    url: str,
    title: str = "IMAGE",
    caption: str | None = None,
    format: str | None = None,
    width: int = IMAGE_WIDTH,
    height: int = IMAGE_HEIGHT,
) -> dict:
    footer = caption or _host_label(url)
    return vstack(
        pad=16,
        gap=8,
        children=[
            text(title.upper(), size="h1", color="white", bg="black", pad=6, weight="bold"),
            image(url=url, format=format, width=width, height=height),
            spacer(),
            text(footer, size="sm", weight="bold"),
        ],
    )
