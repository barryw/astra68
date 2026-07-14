from __future__ import annotations

import struct
import zlib

import pytest

from sw.boot.package_rom import (
    FORMAT_VERSION,
    HEADER_SIZE,
    LOAD_ADDRESS,
    MAGIC,
    ROM_BASE,
    package,
    write_byte_hex,
)


def payload(pc: int = ROM_BASE + 0x400) -> bytes:
    content = bytearray(1024)
    struct.pack_into(">II", content, 0, 0x02000000, pc)
    for index in range(8, len(content)):
        content[index] = (index * 29) & 0xFF
    return bytes(content)


def test_package_header_and_checksums() -> None:
    raw = payload()
    image = package(raw)
    fields = struct.unpack_from(">4sHHIIIIII", image)

    assert fields == (
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        len(raw),
        zlib.crc32(raw),
        ROM_BASE,
        LOAD_ADDRESS,
        0,
        zlib.crc32(image[:28]),
    )
    assert image[HEADER_SIZE:] == raw


def test_package_rejects_invalid_reset_pc() -> None:
    with pytest.raises(ValueError, match="initial PC"):
        package(payload(pc=0x00100000))


def test_write_byte_hex(tmp_path) -> None:
    output = tmp_path / "image.hex"
    write_byte_hex(output, bytes((0x00, 0xA5, 0xFF)))
    assert output.read_text() == "00\na5\nff\n"
