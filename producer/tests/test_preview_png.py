"""Smoke tests for the PIL reference renderer."""

import pytest
from rcink.devices_profile import C3_X4, S3, TLSR
from rcink.layout import divider, grid, hbar, hstack, marker, spacer, sparkline, text, vstack
from rcink.preview_png import render_to_png, unsupported_node_type


def _pixels(img):
    return img.get_flattened_data()


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
            marker(pct=65, color="warning"),
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
    dark_pixels = sum(1 for p in _pixels(g) if p < 64)
    assert dark_pixels > 0, "text should produce some dark pixels"


def test_hbar_labels_change_rendered_pixels():
    unlabeled = render_to_png(vstack(pad=10, children=[hbar(pct=55, color="accent", height=18)]), S3)
    labeled = render_to_png(
        vstack(pad=10, children=[hbar(pct=55, color="accent", height=18, label_left="55", label_right="cap")]),
        S3,
    )

    assert labeled.tobytes() != unlabeled.tobytes()


def test_hbar_in_hstack_gets_visible_flex_width():
    img = render_to_png(
        vstack(
            pad=10,
            children=[
                hstack(
                    gap=4,
                    children=[
                        text("5H", size="body", weight="bold"),
                        hbar(pct=100, color="accent", height=14),
                        text("42%", size="body", weight="bold"),
                    ],
                )
            ],
        ),
        S3,
    )

    red_pixels = sum(1 for p in _pixels(img) if p == (255, 0, 0))
    assert red_pixels > 100


def test_marker_changes_rendered_pixels():
    plain = render_to_png(vstack(pad=10, children=[spacer(size=20)]), S3)
    marked = render_to_png(vstack(pad=10, children=[marker(pct=50, color="accent")]), S3)

    assert marked.tobytes() != plain.tobytes()


def test_grid_renders_heatmap_pixels_on_supported_profile():
    layout = vstack(
        pad=10,
        children=[
            grid(cols=4, rows=2, values=[0, 10, 40, 70, 95, None, 0, 100], gap=2, height=40),
        ],
    )

    img = render_to_png(layout, S3)
    colors = set(_pixels(img))

    assert (255, 0, 0) in colors
    assert (0, 255, 0) in colors
    assert unsupported_node_type(layout, S3) is None


def test_tlsr_grid_is_marked_unsupported():
    layout = vstack(children=[grid(cols=1, rows=1, values=[1])])

    assert unsupported_node_type(layout, TLSR) == "grid"
    with pytest.raises(ValueError, match="tlsr does not support DSL node type 'grid'"):
        render_to_png(layout, TLSR)
