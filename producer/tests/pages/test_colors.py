ALLOWED_COLORS = {"black", "white", "red"}


def assert_bwr_only(node: object) -> None:
    if isinstance(node, dict):
        for key in ("color", "bg"):
            if key in node:
                assert node[key] in ALLOWED_COLORS
        for value in node.values():
            assert_bwr_only(value)
    elif isinstance(node, list):
        for item in node:
            assert_bwr_only(item)
