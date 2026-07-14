#!/usr/bin/env python3
"""Wrap an Astra stage-2 boot image for the SD-card stage-0 loader."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import zlib


MAGIC = b"A68R"
FORMAT_VERSION = 1
HEADER_SIZE = 32
ROM_BASE = 0xFFE00000
ROM_LIMIT = ROM_BASE + 0x00040000
LOAD_ADDRESS = 0x03E00000
MAX_PAYLOAD_SIZE = 0x00040000
RAM_STACK_MIN = 0x01FF8000
RAM_STACK_MAX = 0x02000000


def package(payload: bytes) -> bytes:
    if len(payload) < 8:
        raise ValueError("boot image is too short for reset vectors")
    if len(payload) > MAX_PAYLOAD_SIZE:
        raise ValueError("boot image exceeds the 256 KiB ROM aperture")

    initial_sp, initial_pc = struct.unpack_from(">II", payload)
    if not RAM_STACK_MIN < initial_sp <= RAM_STACK_MAX:
        raise ValueError(f"initial SP 0x{initial_sp:08x} is outside bootstrap RAM")
    if not ROM_BASE <= initial_pc < ROM_LIMIT:
        raise ValueError(f"initial PC 0x{initial_pc:08x} is outside the ROM aperture")

    prefix = struct.pack(
        ">4sHHIIIII",
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        len(payload),
        zlib.crc32(payload),
        ROM_BASE,
        LOAD_ADDRESS,
        0,
    )
    header = prefix + struct.pack(">I", zlib.crc32(prefix))
    if len(header) != HEADER_SIZE:
        raise AssertionError("internal header-size mismatch")
    return header + payload


def write_byte_hex(path: Path, image: bytes) -> None:
    """Write one byte per line for pin-level AstraHost simulations."""
    path.write_text("".join(f"{value:02x}\n" for value in image))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="raw linked astra_boot.bin")
    parser.add_argument("output", type=Path, help="FAT root ASTRA68.ROM image")
    parser.add_argument(
        "--hex-output",
        type=Path,
        help="optional one-byte-per-line copy for RTL simulation",
    )
    args = parser.parse_args()

    payload = args.input.read_bytes()
    image = package(payload)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_bytes(image)
    temporary.replace(args.output)
    if args.hex_output is not None:
        write_byte_hex(args.hex_output, image)
    print(
        f"{args.output}: {len(payload)} payload bytes, "
        f"crc32={zlib.crc32(payload):08x}"
    )


if __name__ == "__main__":
    main()
