"""Rendering-side device profiles.

Each profile captures a panel's viewport (px) and palette resolver. The
producer uses these only for rendering goldens; on the wire the DSL stays
device-agnostic. Each firmware MUST mirror its profile's palette table
in its own palette.c / palette.cpp — divergence is a bug caught by golden
diffs.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from rcink.palette import E6_HEX, normalize_color

PaletteResolver = Callable[[str], str]
"""Map a DSL color token to a `#RRGGBB` string. Unknown token → black."""


@dataclass(frozen=True, slots=True)
class DeviceProfile:
    name: str
    width: int
    height: int
    resolve_color: PaletteResolver
    # Font registry: token → concrete pixel height. Mirrors each firmware's
    # font registry. Used by the reference renderer for text measurement.
    sizes: dict[str, int]


def _resolve_s3(token: str) -> str:
    """s3 is 6-ink Spectra. Semantic tokens map to their natural ink."""
    c = normalize_color(token)
    if c is None:
        return E6_HEX["black"]
    return E6_HEX[c]


def _resolve_c3_x4(token: str) -> str:
    """c3-x4 is B&W. Only `white` remains white; everything else is black."""
    c = normalize_color(token)
    if c == "white":
        return "#FFFFFF"
    return "#000000"


_AMOLED_TABLE: dict[str, str] = {
    "black": "#000000",
    "white": "#FFFFFF",
    "red": "#FF3333",
    "yellow": "#FFC533",
    "green": "#30D158",
    "blue": "#3B82F6",
    "accent": "#FF3333",
    "warning": "#FFC533",
    "success": "#30D158",
    "info": "#3B82F6",
    "muted": "#000000",
}


def _resolve_t_amoled(token: str) -> str:
    """t-amoled is full RGB; semantic tokens map to saturated UI colors."""
    c = normalize_color(token)
    if c is None:
        return _AMOLED_TABLE["black"]
    return _AMOLED_TABLE.get(c, _AMOLED_TABLE["black"])


# tlsr (BWR) physical inks
_TLSR_BLACK = "#000000"
_TLSR_WHITE = "#FFFFFF"
_TLSR_RED = "#FF0000"

_TLSR_TABLE: dict[str, str] = {
    "black": _TLSR_BLACK,
    "white": _TLSR_WHITE,
    "red": _TLSR_RED,
    # legacy concrete: collapse to BWR per firmware palette.c
    "yellow": _TLSR_BLACK,
    "green": _TLSR_BLACK,
    "blue": _TLSR_RED,
    # semantic
    "accent": _TLSR_RED,
    "warning": _TLSR_RED,
    "success": _TLSR_BLACK,
    "info": _TLSR_BLACK,
    "muted": _TLSR_BLACK,
}


def _resolve_tlsr(token: str) -> str:
    c = normalize_color(token)
    if c is None:
        return _TLSR_BLACK
    return _TLSR_TABLE.get(c, _TLSR_BLACK)


# Font registries — mirror each firmware's px settings per the design spec.
# Lowercase aliases included because rcpaper builders historically use lowercase.
_S3_SIZES: dict[str, int] = {
    "H1": 60,
    "H2": 32,
    "Body": 20,
    "Sm": 14,
    "h1": 60,
    "h2": 32,
    "body": 20,
    "sm": 14,
}
_C3_X4_SIZES: dict[str, int] = {
    "H1": 56,
    "H2": 28,
    "Body": 18,
    "Sm": 12,
    "h1": 56,
    "h2": 28,
    "body": 18,
    "sm": 12,
}
_TLSR_SIZES: dict[str, int] = {
    "H1": 24,
    "H2": 16,
    "Body": 12,
    "Sm": 10,
    "h1": 24,
    "h2": 16,
    "body": 12,
    "sm": 10,
}
_T_AMOLED_SIZES: dict[str, int] = {
    "H1": 52,
    "H2": 26,
    "Body": 16,
    "Sm": 8,
    "h1": 52,
    "h2": 26,
    "body": 16,
    "sm": 8,
}


S3 = DeviceProfile(name="s3", width=400, height=600, resolve_color=_resolve_s3, sizes=_S3_SIZES)
C3_X4 = DeviceProfile(name="c3-x4", width=800, height=480, resolve_color=_resolve_c3_x4, sizes=_C3_X4_SIZES)
TLSR = DeviceProfile(name="tlsr", width=250, height=128, resolve_color=_resolve_tlsr, sizes=_TLSR_SIZES)
T_AMOLED = DeviceProfile(
    name="t-amoled",
    width=240,
    height=536,
    resolve_color=_resolve_t_amoled,
    sizes=_T_AMOLED_SIZES,
)

PROFILES: dict[str, DeviceProfile] = {S3.name: S3, C3_X4.name: C3_X4, TLSR.name: TLSR, T_AMOLED.name: T_AMOLED}


def get_profile(name: str) -> DeviceProfile:
    if name not in PROFILES:
        raise KeyError(f"unknown device profile {name!r}; known: {sorted(PROFILES)}")
    return PROFILES[name]
