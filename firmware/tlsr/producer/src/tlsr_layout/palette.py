"""Six-color palette used by rcpapercolor; firmware maps unsupported names."""

COLORS: frozenset[str] = frozenset({"black", "white", "red", "yellow", "green", "blue"})


def is_valid_color(name: object) -> bool:
    return isinstance(name, str) and name in COLORS


def validate_color(name: str | None, *, allow_none: bool = False) -> str | None:
    if name is None:
        if allow_none:
            return None
        raise ValueError("color is required")
    if name not in COLORS:
        raise ValueError(f"unknown color {name!r}; expected one of {sorted(COLORS)}")
    return name
