"""Create the MINIR3's SDK-format OTA image from its raw ESPHome binary."""

import hashlib
from pathlib import Path
import struct
import zlib

import esphome.config_validation as cv
from esphome.core import CORE

MINIR3_BOARD = "sonoff-minir3"
MINIR3_INACTIVE_SLOT_SIZE = 0xB4000
BOOTINFO_SIZE = 0x1000
BOOT_HEADER_SIZE = 176
OTA_HEADER_SIZE = 512
OTA_MAGIC = b"BL60X_OTA_Ver1.0"
BOOT_HEADER_TEMPLATE = Path(__file__).with_name("minir3_boot_header.bin")


def _version_field(value: str, field: str) -> bytes:
    value_bytes = value.encode("ascii")
    if not value_bytes or len(value_bytes) > 15:
        raise cv.Invalid(f"BL602 OTA {field} must be 1-15 ASCII characters")
    return value_bytes.ljust(16, b"\0")


def _raw_application(firmware: bytes) -> bytes:
    """Accept either the SDK's raw linked image or its single-image wrapper.

    LibreTiny's BL602 build output is `img_single.bin`: it already starts with
    a BFNP boot header and a 4 KiB image header page. The network OTA package
    needs the linked application bytes only, because it creates a fresh boot
    info page for the inactive slot. Wrapping the complete SDK image here would
    put a second BFNP image at the application's entry point and fail to boot.
    """
    if not firmware.startswith(b"BFNP"):
        return firmware
    if len(firmware) < BOOTINFO_SIZE:
        raise cv.Invalid("BL602 firmware has a truncated BFNP boot header page")

    header = firmware[:BOOT_HEADER_SIZE]
    stored_crc = struct.unpack_from("<I", header, 172)[0]
    if (zlib.crc32(header[:172]) & 0xFFFFFFFF) != stored_crc:
        raise cv.Invalid("BL602 firmware BFNP boot header CRC is invalid")
    app_size = struct.unpack_from("<I", header, 120)[0]
    app = firmware[BOOTINFO_SIZE : BOOTINFO_SIZE + app_size]
    if app_size == 0 or len(app) != app_size:
        raise cv.Invalid("BL602 firmware BFNP application length is invalid")
    if hashlib.sha256(app).digest() != header[132:164]:
        raise cv.Invalid("BL602 firmware BFNP application SHA-256 is invalid")
    return app


def _make_image(header_template: bytes, raw_firmware: bytes) -> bytes:
    if len(header_template) != BOOT_HEADER_SIZE or header_template[:4] != b"BFNP":
        raise cv.Invalid("MINIR3 boot header template is missing or has the wrong size")
    header = bytearray(header_template)
    stored_crc = struct.unpack_from("<I", header, 172)[0]
    if (zlib.crc32(header[:172]) & 0xFFFFFFFF) != stored_crc:
        raise cv.Invalid("MINIR3 boot header template CRC is invalid")
    if header[116] & 0x0F:
        raise cv.Invalid("signed or encrypted MINIR3 boot headers are not supported")
    if not raw_firmware:
        raise cv.Invalid("BL602 firmware binary is empty")

    raw_firmware = _raw_application(raw_firmware)

    firmware = raw_firmware + b"\xff" * ((-len(raw_firmware)) % 16)
    body = bytearray(header)
    struct.pack_into("<I", body, 120, len(firmware))
    body[132:164] = hashlib.sha256(firmware).digest()
    struct.pack_into("<I", body, 172, zlib.crc32(body[:172]) & 0xFFFFFFFF)
    bootinfo = bytes(body) + b"\xff" * (BOOTINFO_SIZE - len(body))
    payload = bootinfo + firmware
    # The SDK OTA receiver writes a 32-byte SHA trailer into the inactive slot.
    if len(payload) + 32 > MINIR3_INACTIVE_SLOT_SIZE:
        raise cv.Invalid(
            f"MINIR3 OTA payload {len(payload) + 32} bytes exceeds inactive slot "
            f"{MINIR3_INACTIVE_SLOT_SIZE} bytes"
        )

    ota_header = bytearray()
    ota_header.extend(OTA_MAGIC)
    ota_header.extend(b"RAW ")
    ota_header.extend(struct.pack("<I", len(payload)))
    ota_header.extend(bytes(range(1, 9)))
    ota_header.extend(_version_field("MINIR3", "hardware version"))
    ota_header.extend(_version_field("ESPHome", "software version"))
    ota_header.extend(hashlib.sha256(payload).digest())
    ota_header.extend(struct.pack("<I", len(payload)))
    ota_header.extend(b"\xff" * (OTA_HEADER_SIZE - len(ota_header)))
    return bytes(ota_header) + payload


def device_ota_image(firmware: Path) -> Path:
    """Wrap only the named MINIR3 board's raw image for the BL602 OTA backend."""
    board = CORE.config.get("bl60x", {}).get("board")
    if board != MINIR3_BOARD:
        raise cv.Invalid(
            "BL602 network OTA images are only available for the verified "
            "`sonoff-minir3` board profile"
        )
    image = _make_image(BOOT_HEADER_TEMPLATE.read_bytes(), firmware.read_bytes())
    output = CORE.relative_pioenvs_path(CORE.name, "firmware.ota.bin")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    return output
