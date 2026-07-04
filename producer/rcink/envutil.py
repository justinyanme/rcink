"""Environment helpers."""

from __future__ import annotations

import os


def require_env(key: str) -> str:
    value = os.environ.get(key)
    if not value:
        raise RuntimeError(f"{key} not set in environment")
    return value
