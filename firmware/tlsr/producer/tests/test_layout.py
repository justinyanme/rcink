import pytest

from tlsr_layout.layout import (
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


def test_vstack_drops_none_values():
    node = vstack()
    assert node == {"type": "vstack", "children": []}


def test_vstack_keeps_explicit_zero():
    node = vstack(gap=0, pad=0)
    assert node["gap"] == 0
    assert node["pad"] == 0


def test_vstack_keeps_children():
    leaf = text("hi", color="black")
    node = vstack(children=[leaf])
    assert node["children"] == [leaf]


def test_hstack_shape():
    node = hstack(gap=4, pad=2, align="center", children=[])
    assert node == {
        "type": "hstack",
        "gap": 4,
        "pad": 2,
        "align": "center",
        "children": [],
    }


def test_text_minimal():
    assert text("hello") == {"type": "text", "value": "hello"}


def test_text_full():
    node = text(
        "X",
        size="h1",
        color="white",
        weight="bold",
        align="center",
        bg="red",
        pad=8,
    )
    assert node == {
        "type": "text",
        "value": "X",
        "size": "h1",
        "color": "white",
        "weight": "bold",
        "align": "center",
        "bg": "red",
        "pad": 8,
    }


def test_text_rejects_unknown_color():
    with pytest.raises(ValueError, match="unknown color 'purple'"):
        text("X", color="purple")


def test_text_rejects_unknown_bg():
    with pytest.raises(ValueError, match="unknown color 'mauve'"):
        text("X", bg="mauve")


def test_spacer_no_size():
    assert spacer() == {"type": "spacer"}


def test_spacer_with_size():
    assert spacer(size=12) == {"type": "spacer", "size": 12}


def test_divider_validates_color():
    with pytest.raises(ValueError):
        divider(color="not-a-color")
    assert divider(color="black", thickness=2) == {
        "type": "divider",
        "color": "black",
        "thickness": 2,
    }


def test_hbar_minimal_requires_pct():
    node = hbar(pct=42)
    assert node == {"type": "hbar", "pct": 42}


def test_hbar_full():
    node = hbar(
        pct=42,
        color="blue",
        height=14,
        label_left="A",
        label_right="B",
    )
    assert node["color"] == "blue"
    assert node["label_left"] == "A"


def test_icon():
    assert icon("cpu", size=24, color="red") == {
        "type": "icon",
        "name": "cpu",
        "size": 24,
        "color": "red",
    }


def test_image_requires_exactly_one_of_name_or_url():
    with pytest.raises(ValueError, match="exactly one"):
        image()
    with pytest.raises(ValueError, match="exactly one"):
        image(name="x", url="http://y")
    assert image(name="x")["name"] == "x"
    assert image(url="http://y")["url"] == "http://y"


def test_sparkline_copies_values():
    src = [1, 2, 3]
    node = sparkline(src, color="blue", height=20, baseline="auto")
    assert node["values"] == [1, 2, 3]
    src.append(4)
    assert node["values"] == [1, 2, 3]  # producer copy is independent


def test_marker():
    assert marker(pct=50, color="red") == {"type": "marker", "pct": 50, "color": "red"}
