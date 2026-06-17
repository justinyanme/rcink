from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def _load_capture_module():
    root = Path(__file__).resolve().parents[2]
    path = root / "firmware" / "rlcd" / "tools" / "capture_frames.py"
    spec = importlib.util.spec_from_file_location("rlcd_capture_frames", path)
    assert spec is not None
    assert spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


def _clear_pixel(buf: bytearray, *, x: int, y: int, w: int, h: int) -> None:
    blocks_y = h // 4
    inv_y = h - 1 - y
    idx = (x >> 1) * blocks_y + (inv_y >> 2)
    bit = 7 - (((inv_y & 3) << 1) | (x & 1))
    buf[idx] &= ~(1 << bit)


def test_unpack_st7305_frame_uses_firmware_orientation() -> None:
    capture = _load_capture_module()
    w, h = 4, 8
    buf = bytearray([0xFF] * ((w * h) // 8))
    _clear_pixel(buf, x=0, y=7, w=w, h=h)
    _clear_pixel(buf, x=3, y=0, w=w, h=h)

    img = capture.unpack_st7305_frame(bytes(buf), width=w, height=h)

    assert img.getpixel((0, 7)) == (0, 0, 0)
    assert img.getpixel((3, 0)) == (0, 0, 0)
    assert img.getpixel((0, 0)) == (255, 255, 255)
    assert img.getpixel((3, 7)) == (255, 255, 255)
