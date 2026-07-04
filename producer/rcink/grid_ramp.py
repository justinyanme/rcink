"""Shared heatmap intensity ramp for grid nodes."""

from __future__ import annotations

from typing import Literal

GridRampToken = Literal["white", "yellow", "green", "blue", "red"]


def grid_color_token(value: float | int | None) -> GridRampToken:
    if value is None or value <= 0:
        return "white"
    if value < 30:
        return "yellow"
    if value < 65:
        return "green"
    if value < 90:
        return "blue"
    return "red"
