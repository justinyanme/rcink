"""Device registry — what physical units we know about, and how we reach them.

For v1 this is hand-written. v1.1+ may move to a TOML/YAML config.
"""

from dataclasses import dataclass
from typing import Literal

Transport = Literal["axiom", "ble"]


@dataclass(frozen=True, slots=True)
class Device:
    name: str  # short id used in CLI ("s3-home", "x4-desk", "tlsr-001")
    kind: str  # "s3" | "c3-x4" | "tlsr"
    transport: Transport
    ble_mac: str | None = None  # required when transport == "ble"
    axiom_dataset: str | None = None  # defaults to AXIOM_DATASET env


# Default registry — overridable via environment in v1.1+.
DEVICES: tuple[Device, ...] = (
    Device(name="s3-home", kind="s3", transport="axiom"),
    Device(name="t-amoled-desk", kind="t-amoled", transport="axiom"),
)


def get(name: str) -> Device:
    for d in DEVICES:
        if d.name == name:
            return d
    raise KeyError(f"unknown device {name!r}; known: {[d.name for d in DEVICES]}")
