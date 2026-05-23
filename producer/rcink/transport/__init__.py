"""Transport router — dispatches a page payload to a device's transport."""

from rcink import axiom as _axiom
from rcink.devices import Device


def push(device: Device, page_id: str, layout: dict, order: int = 0) -> None:
    """Push a built page to a device via its configured transport."""
    if device.transport == "axiom":
        # axiom.ingest uses AXIOM_DATASET env when dataset arg omitted
        _axiom.ingest(id=page_id, layout=layout, order=order)
        return
    if device.transport == "ble":
        # Lazy import — bleak heavy, only needed for BLE devices.
        from rcink.transport import ble  # noqa: F401

        raise NotImplementedError("BLE push wiring lands in Plan 3 (rcink-ble-pull-and-goldens)")
    raise ValueError(f"unknown transport {device.transport!r}")
