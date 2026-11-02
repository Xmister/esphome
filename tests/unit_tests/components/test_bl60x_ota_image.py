import hashlib
from pathlib import Path
import struct
import zlib

import pytest

from esphome.components.bl60x.ota_image import (
    BOOT_HEADER_SIZE,
    BOOTINFO_SIZE,
    OTA_HEADER_SIZE,
    _make_image,
    _raw_application,
)
import esphome.config_validation as cv


def _sdk_image(app: bytes) -> bytes:
    header = bytearray(
        (
            Path(__file__).parents[3]
            / "esphome/components/bl60x/minir3_boot_header.bin"
        ).read_bytes()
    )
    app = app + b"\xff" * ((-len(app)) % 16)
    struct.pack_into("<I", header, 120, len(app))
    header[132:164] = hashlib.sha256(app).digest()
    struct.pack_into("<I", header, 172, zlib.crc32(header[:172]) & 0xFFFFFFFF)
    return bytes(header) + b"\xff" * (BOOTINFO_SIZE - BOOT_HEADER_SIZE) + app


def test_ota_image_extracts_and_rewraps_sdk_single_image() -> None:
    app = b"linked app instructions" * 31
    wrapped = _make_image(
        Path("esphome/components/bl60x/minir3_boot_header.bin").read_bytes(),
        _sdk_image(app),
    )

    assert wrapped.startswith(b"BL60X_OTA_Ver1.0RAW ")
    assert len(wrapped) >= OTA_HEADER_SIZE + BOOTINFO_SIZE
    payload_size = struct.unpack_from("<I", wrapped, 20)[0]
    payload = wrapped[OTA_HEADER_SIZE : OTA_HEADER_SIZE + payload_size]
    assert payload[:4] == b"BFNP"
    assert struct.unpack_from("<I", payload, 120)[0] == len(
        app + b"\xff" * ((-len(app)) % 16)
    )
    assert payload[BOOTINFO_SIZE:] == app + b"\xff" * ((-len(app)) % 16)
    assert hashlib.sha256(payload[BOOTINFO_SIZE:]).digest() == payload[132:164]


def test_raw_linked_application_is_still_accepted() -> None:
    app = b"raw linked application"
    assert _raw_application(app) == app


@pytest.mark.parametrize("corruption", ["crc", "sha", "length"])
def test_invalid_sdk_single_image_is_rejected(corruption: str) -> None:
    image = bytearray(_sdk_image(b"valid application"))
    if corruption == "crc":
        image[172] ^= 1
    elif corruption == "sha":
        image[132] ^= 1
        struct.pack_into("<I", image, 172, zlib.crc32(image[:172]) & 0xFFFFFFFF)
    else:
        struct.pack_into("<I", image, 120, len(image))
        struct.pack_into("<I", image, 172, zlib.crc32(image[:172]) & 0xFFFFFFFF)

    with pytest.raises(cv.Invalid):
        _raw_application(bytes(image))
