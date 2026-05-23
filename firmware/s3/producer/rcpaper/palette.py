"""E6 color palette shared by producer layouts and browser preview."""

from __future__ import annotations

from typing import Final, Literal, TypeAlias, cast

ColorToken: TypeAlias = Literal["black", "white", "red", "yellow", "green", "blue"]

E6_COLOR_TOKENS: Final[tuple[ColorToken, ...]] = (
    "black",
    "white",
    "red",
    "yellow",
    "green",
    "blue",
)

E6_HEX: Final[dict[ColorToken, str]] = {
    "black": "#000000",
    "white": "#FFFFFF",
    "red": "#FF0000",
    "yellow": "#FFFF00",
    "green": "#00FF00",
    "blue": "#0000FF",
}


def normalize_color(token: str | None, *, field: str = "color") -> ColorToken | None:
    if token is None:
        return None
    if token in E6_HEX:
        return cast(ColorToken, token)
    allowed = ", ".join(E6_COLOR_TOKENS)
    raise ValueError(f"{field} must be one of the six E6 colors ({allowed}); got {token!r}")
