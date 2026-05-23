"""Smoke tests for the PIL reference renderer."""

import pytest
from rcink.devices_profile import C3_X4, S3, TLSR
from rcink.layout import divider, hbar, hstack, spacer, sparkline, text, vstack
from rcink.preview_png import render_to_png


@pytest.fixture
def kitchen_sink():
    """One layout that exercises every primitive."""
    return vstack(
        pad=8,
        gap=4,
        children=[
            text("HELLO", size="h1", color="accent", weight="bold", align="center"),
            divider(color="black", thickness=2),
            hbar(pct=42, color="accent", height=12, label_left="42%"),
            sparkline([1, 5, 3, 7, 4, 8, 6], color="info", height=24),
            hstack(
                gap=4,
                children=[
                    text("LEFT", size="body", color="success"),
                    spacer(),
                    text("RIGHT", size="body", color="warning"),
                ],
            ),
            spacer(size=8),
            text("muted footer", size="sm", color="muted", align="center"),
        ],
    )


@pytest.mark.parametrize("prof", [S3, C3_X4, TLSR])
def test_renders_kitchen_sink_at_viewport(kitchen_sink, prof):
    img = render_to_png(kitchen_sink, prof)
    assert img.size == (prof.width, prof.height)
    assert img.mode == "RGB"


def test_empty_vstack_renders_blank_canvas():
    img = render_to_png(vstack(children=[]), S3)
    px = img.load()
    # Top-left corner should be background white.
    assert px[10, 10] == (255, 255, 255)


def test_text_renders_some_dark_pixels_on_s3():
    img = render_to_png(
        vstack(pad=10, children=[text("XXXXXX", size="h1", color="black")]),
        S3,
    )
    g = img.convert("L")
    dark_pixels = sum(1 for p in g.getdata() if p < 64)
    assert dark_pixels > 0, "text should produce some dark pixels"
