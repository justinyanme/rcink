import pytest
from rcink.layout import (
    divider,
    hbar,
    hstack,
    icon,
    image,
    marker,
    spacer,
    sparkline,
    text,
    vstack,
)
from rcink.palette import E6_COLOR_TOKENS, E6_HEX
from rcink.preview import to_html


def test_text_minimal():
    assert text("CLAUDE") == {"type": "text", "value": "CLAUDE"}


def test_text_full():
    assert text(
        "CLAUDE",
        size="h1",
        color="white",
        weight="bold",
        align="center",
        bg="red",
        pad=8,
    ) == {
        "type": "text",
        "value": "CLAUDE",
        "size": "h1",
        "color": "white",
        "weight": "bold",
        "align": "center",
        "bg": "red",
        "pad": 8,
    }


def test_vstack_children():
    node = vstack(pad=14, gap=6, align="center", children=[text("A"), text("B")])
    assert node == {
        "type": "vstack",
        "pad": 14,
        "gap": 6,
        "align": "center",
        "children": [
            {"type": "text", "value": "A"},
            {"type": "text", "value": "B"},
        ],
    }


def test_hstack_default():
    assert hstack(children=[]) == {"type": "hstack", "children": []}


def test_spacer_sized_and_flex():
    assert spacer(size=12) == {"type": "spacer", "size": 12}
    assert spacer() == {"type": "spacer"}


def test_divider():
    assert divider(color="black", thickness=2) == {
        "type": "divider",
        "color": "black",
        "thickness": 2,
    }


def test_hbar():
    assert hbar(pct=42, color="blue", height=24, label_right="42%") == {
        "type": "hbar",
        "pct": 42,
        "color": "blue",
        "height": 24,
        "label_right": "42%",
    }


def test_icon_image():
    assert icon("claude", size=20, color="red") == {
        "type": "icon",
        "name": "claude",
        "size": 20,
        "color": "red",
    }
    assert image("logo", width=80, height=80) == {
        "type": "image",
        "name": "logo",
        "width": 80,
        "height": 80,
    }
    assert image(url="https://example.com/paper.png", format="png", width=80, height=80) == {
        "type": "image",
        "url": "https://example.com/paper.png",
        "format": "png",
        "width": 80,
        "height": 80,
    }


def test_sparkline_marker():
    assert sparkline([1, 2, 3], color="blue", height=40) == {
        "type": "sparkline",
        "values": [1, 2, 3],
        "color": "blue",
        "height": 40,
    }
    assert marker(pct=80, color="red") == {
        "type": "marker",
        "pct": 80,
        "color": "red",
    }


def test_omits_none_keys():
    # We never serialize None — the firmware applies its own defaults
    assert "color" not in text("X")
    assert "size" not in spacer()


def test_image_requires_exactly_one_source():
    try:
        image(width=80, height=80)
    except ValueError as exc:
        assert "exactly one" in str(exc)
    else:  # pragma: no cover - defensive
        raise AssertionError("image() should reject missing source")


def test_color_tokens_concrete_e6_palette():
    assert E6_COLOR_TOKENS == ("black", "white", "red", "yellow", "green", "blue")
    # The 6 concrete inks must each have a hex mapping
    for token in E6_COLOR_TOKENS:
        assert token in E6_HEX


def test_color_tokens_semantic_aliases_resolve_to_s3_inks():
    # Semantic tokens preview as their s3 (6-ink) target color
    assert E6_HEX["accent"] == E6_HEX["red"]
    assert E6_HEX["warning"] == E6_HEX["yellow"]
    assert E6_HEX["success"] == E6_HEX["green"]
    assert E6_HEX["info"] == E6_HEX["blue"]
    assert E6_HEX["muted"] == E6_HEX["black"]


def test_color_tokens_reject_unknown():
    with pytest.raises(ValueError, match="DSL color tokens"):
        text("X", color="magenta")
    with pytest.raises(ValueError, match="DSL color tokens"):
        text("X", bg="gray")


def test_preview_uses_solid_e6_colors():
    html = to_html(text("X", color="red", bg="blue"))
    assert "#FF0000" in html
    assert "#0000FF" in html
    assert "#D92027" not in html
    assert "#0F4D92" not in html
