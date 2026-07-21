"""Color palette shared by producer layouts and browser preview.

The DSL accepts two kinds of color tokens:

- **Concrete** — the 6 physical inks of the rcpaper s3's Spectra 6 panel
  (`black`, `white`, `red`, `yellow`, `green`, `blue`). These map 1:1 on s3,
  and are reduced per panel on other devices (e.g. tlsr BWR drops yellow/green/blue
  to black or red).

- **Semantic** — intent-named tokens (`accent`, `warning`, `success`, `info`,
  `muted`) introduced in DSL v1. Each device resolves these to its best
  approximation. On s3 they map to {red, yellow, green, blue, black}
  respectively; on tlsr (BWR) most collapse to black or red; on c3-x4 (B&W)
  everything but `white` collapses to black.

Producer code passes tokens through unchanged — resolution happens in firmware.
This module only validates that the token is a known one, and provides a
preview hex mapping used by the browser preview tool.
"""

from __future__ import annotations

from typing import Final, Literal, cast

type ConcreteColorToken = Literal["black", "white", "red", "yellow", "green", "blue"]
type SemanticColorToken = Literal["accent", "warning", "success", "info", "muted"]
type ColorToken = ConcreteColorToken | SemanticColorToken

E6_COLOR_TOKENS: Final[tuple[ConcreteColorToken, ...]] = (
    "black",
    "white",
    "red",
    "yellow",
    "green",
    "blue",
)

SEMANTIC_COLOR_TOKENS: Final[tuple[SemanticColorToken, ...]] = (
    "accent",
    "warning",
    "success",
    "info",
    "muted",
)

ALL_COLOR_TOKENS: Final[tuple[ColorToken, ...]] = E6_COLOR_TOKENS + SEMANTIC_COLOR_TOKENS

# Preview hex — browser preview is the most-capable rendering target (full RGB)
# so semantic tokens resolve to the s3 column of the spec's mapping table.
E6_HEX: Final[dict[ColorToken, str]] = {
    "black": "#000000",
    "white": "#FFFFFF",
    "red": "#FF0000",
    "yellow": "#FFFF00",
    "green": "#00FF00",
    "blue": "#0000FF",
    # Semantic → s3 inks (used for HTML preview)
    "accent": "#FF0000",
    "warning": "#FFFF00",
    "success": "#00FF00",
    "info": "#0000FF",
    "muted": "#000000",
}


def normalize_color(token: str | None, *, field: str = "color") -> ColorToken | None:
    if token is None:
        return None
    if token in E6_HEX:
        return cast(ColorToken, token)
    allowed = ", ".join(ALL_COLOR_TOKENS)
    raise ValueError(f"{field} must be one of the DSL color tokens ({allowed}); got {token!r}")
