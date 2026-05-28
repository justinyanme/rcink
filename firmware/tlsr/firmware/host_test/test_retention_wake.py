from pathlib import Path


def _function_body(source: str, name: str) -> str:
    start = source.index(f"void {name}(void)")
    open_brace = source.index("{", start)
    depth = 0
    for pos in range(open_brace, len(source)):
        char = source[pos]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : pos]
    raise AssertionError(f"could not find body for {name}")


def test_deep_retention_wake_rearms_layout_xfer_service():
    app_c = Path(__file__).resolve().parents[1] / "src" / "app.c"
    body = _function_body(app_c.read_text(), "user_init_deepRetn")

    assert "blc_ll_recoverDeepRetention();" in body
    assert "layout_xfer_service_init();" in body
    assert body.index("blc_ll_recoverDeepRetention();") < body.index(
        "layout_xfer_service_init();"
    )
