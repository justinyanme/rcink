from rcpaper.layout import (
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
        max_lines=1,
    ) == {
        "type": "text",
        "value": "CLAUDE",
        "size": "h1",
        "color": "white",
        "weight": "bold",
        "align": "center",
        "bg": "red",
        "pad": 8,
        "max_lines": 1,
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
