from rcink.pages.image import IMAGE_HEIGHT, IMAGE_WIDTH, build_image_page


def test_image_page_uses_remote_image_node():
    page = build_image_page(url="https://example.com/frame.png")
    image_node = page["children"][1]
    assert image_node == {
        "type": "image",
        "url": "https://example.com/frame.png",
        "width": IMAGE_WIDTH,
        "height": IMAGE_HEIGHT,
    }


def test_image_page_defaults_footer_to_hostname():
    page = build_image_page(url="https://www.example.com/frame.png")
    footer = page["children"][-1]
    assert footer["type"] == "text"
    assert footer["value"] == "example.com"


def test_image_page_keeps_explicit_caption_and_format():
    page = build_image_page(
        url="https://cdn.example.com/frame",
        caption="midjourney drop",
        format="jpg",
    )
    assert page["children"][1]["format"] == "jpg"
    assert page["children"][-1]["value"] == "midjourney drop"
