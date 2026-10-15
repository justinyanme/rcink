#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["esptool==4.9.0", "typer>=0.12"]
# ///
"""Verify PaperColor identity/partitions, flash app only, then watchdog-reset."""

import hashlib
import json
from pathlib import Path
from typing import Annotated

import esptool
import typer
from esptool.targets.esp32s3 import ESP32S3ROM

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "firmware/papercolor/.pio/build/m5stack-papercolor"
LAB_BUILD = ROOT / "firmware/papercolor-lab/.pio/build/m5stack-papercolor-lab"
MAC = "44:1b:f6:c1:31:90"


def main(
    port: Annotated[str, typer.Option(help="Exact PaperColor port; never autodiscovered/retried.")],
    out: Annotated[Path, typer.Option(help="New directory for the flash manifest.")],
    build_dir: Annotated[
        Path, typer.Option(help=f"PlatformIO build dir holding firmware.bin + partitions.bin; Lab: {LAB_BUILD}")
    ] = BUILD,
) -> None:
    firmware = build_dir / "firmware.bin"
    partitions = (build_dir / "partitions.bin").read_bytes()
    app = firmware.read_bytes()
    if not app or app[0] != 0xE9 or len(app) > 0x500000:
        raise typer.BadParameter("Missing/invalid/oversized ESP application")
    if not Path(port).exists():
        raise typer.BadParameter(f"Port absent: {port}; wake board explicitly, do not race enumeration")
    out.mkdir(parents=True, exist_ok=False, mode=0o700)
    esp = ESP32S3ROM(port, baud=115200)
    try:
        # This target is the native USB-Serial/JTAG peripheral, not a UART
        # bridge. Keep one open port; allow bounded ROM synchronization.
        esp.connect(mode="usb_reset", attempts=3)
        mac = ":".join(f"{byte:02x}" for byte in esp.read_mac())
        if mac != MAC:
            raise RuntimeError(f"Wrong target: {mac}; expected {MAC}; no flash writes performed")
        print(f"Verified PaperColor {mac}; checking partition table", flush=True)
        esp = esp.run_stub()
        esp.change_baud(921600)
        live_partitions = esp.read_flash(0x8000, len(partitions))
        if live_partitions != partitions:
            raise RuntimeError("Live partition table differs from build; refusing app-only flash")
        manifest = {
            "mac": mac,
            "port": port,
            "firmware": str(firmware),
            "firmware_sha256": hashlib.sha256(app).hexdigest(),
            "app_offset": 0x10000,
            "app_size": len(app),
            "state": "identity-partitions-verified",
        }
        manifest_path = out / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        # Existing loader object keeps the very same port open. esptool verifies
        # written data before its successful watchdog reset; failures stay stopped.
        esp.sync()  # Identify the running stub so main() reuses it, without uploading it again.
        esptool.main(
            ["--chip", "esp32s3", "--port", port, "--after", "watchdog_reset", "write_flash", "0x10000", str(firmware)],
            esp=esp,
        )
        manifest["state"] = "flashed-verified-reset"
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        print(f"Flash manifest: {out}")
    finally:
        esp._port.close()


if __name__ == "__main__":
    cli = typer.Typer(pretty_exceptions_show_locals=False)
    cli.command()(main)
    cli()
