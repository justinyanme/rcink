#!/usr/bin/env python3

import binascii
import sys

OTA_MAGIC = b"\x5d\x02"
MIN_FIRMWARE_HEADER_BYTES = 0x1C

if len(sys.argv) != 2:
    raise SystemExit("Usage: tl_check_fw.py <firmware.bin>")

# https://github.com/pvvx/ATC_MiThermometer/issues/186#issuecomment-1030410603
with open(sys.argv[1], "rb") as f:
    firmware = bytearray(f.read())

if len(firmware) < MIN_FIRMWARE_HEADER_BYTES:
    raise ValueError(f"firmware is too short for TLSR header: {len(firmware)} bytes")

if firmware[6:8] != OTA_MAGIC:
    # Ensure FW size is multiple of 16
    padding = 16 - len(firmware) % 16
    if padding < 16:
        firmware += b"\xff" * padding
    # Fix FW length
    firmware[0x18:0x1C] = (len(firmware) + 4).to_bytes(4, byteorder="little")
    # Add magic constant
    firmware[6:8] = OTA_MAGIC
    # Add CRC
    crc = binascii.crc32(firmware) ^ 0xFFFFFFFF
    firmware += crc.to_bytes(4, byteorder="little")
    # Write the new firmware back to the file
    with open(sys.argv[1], "wb") as f:
        f.write(firmware)
    print("Firmware patched!")
else:
    print("Firmware already patched!")
