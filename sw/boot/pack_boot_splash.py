#!/usr/bin/env python3
"""Build the bounded INDEX8/LZ4 boot-splash payload."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
WIDTH = 720
HEIGHT = 480
PIXEL_BYTES = WIDTH * HEIGHT
SOURCE_PALETTE_LIMIT = 252
PALETTE_BYTES = 256 * 4
FONT_GLYPHS = 256
FONT_SOURCE_ROWS = 8
FONT_ROWS = 16
FONT_BYTES = FONT_GLYPHS * FONT_ROWS
RAW_BYTES = PIXEL_BYTES + PALETTE_BYTES + FONT_BYTES

# Entries 252..255 are unavailable to the background image and remain stable
# for hardware-rendered status text.
STATUS_COLORS_RGB = (
    (0x38, 0xEC, 0xF2),  # cyan text
    (0xFF, 0x94, 0x18),  # orange accent
    (0xFF, 0xB3, 0x2C),  # warm success
    (0xFF, 0x4D, 0x63),  # failure
)


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def decode_indexed_png(path: Path) -> tuple[bytes, list[tuple[int, int, int]]]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("splash source is not a PNG")

    offset = len(PNG_SIGNATURE)
    width = height = bit_depth = color_type = interlace = None
    palette: list[tuple[int, int, int]] | None = None
    compressed = bytearray()
    saw_end = False

    while offset < len(data):
        if len(data) - offset < 12:
            raise ValueError("truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4 : offset + 8]
        payload_start = offset + 8
        payload_end = payload_start + length
        crc_end = payload_end + 4
        if crc_end > len(data):
            raise ValueError("PNG chunk exceeds file")
        payload = data[payload_start:payload_end]
        expected_crc = struct.unpack_from(">I", data, payload_end)[0]
        actual_crc = zlib.crc32(chunk_type)
        actual_crc = zlib.crc32(payload, actual_crc) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise ValueError(f"bad PNG CRC for {chunk_type!r}")

        if chunk_type == b"IHDR":
            if length != 13:
                raise ValueError("invalid PNG IHDR size")
            (width, height, bit_depth, color_type, compression,
             filtering, interlace) = struct.unpack(">IIBBBBB", payload)
            if compression != 0 or filtering != 0:
                raise ValueError("unsupported PNG compression/filter method")
        elif chunk_type == b"PLTE":
            if length == 0 or length % 3 != 0:
                raise ValueError("invalid PNG palette")
            palette = [tuple(payload[index : index + 3])
                       for index in range(0, length, 3)]
        elif chunk_type == b"IDAT":
            compressed.extend(payload)
        elif chunk_type == b"IEND":
            saw_end = True
            break
        offset = crc_end

    if not saw_end:
        raise ValueError("PNG has no IEND")
    if (width, height, bit_depth, color_type, interlace) != (
            WIDTH, HEIGHT, 8, 3, 0):
        raise ValueError("splash must be non-interlaced 720x480 indexed PNG8")
    if palette is None or len(palette) > SOURCE_PALETTE_LIMIT:
        raise ValueError("splash source must use at most 252 palette entries")

    filtered = zlib.decompress(bytes(compressed))
    expected_filtered = HEIGHT * (WIDTH + 1)
    if len(filtered) != expected_filtered:
        raise ValueError("unexpected indexed PNG scanline size")

    pixels = bytearray(PIXEL_BYTES)
    previous = bytearray(WIDTH)
    source_offset = 0
    destination_offset = 0
    for _ in range(HEIGHT):
        filter_type = filtered[source_offset]
        source_offset += 1
        encoded = filtered[source_offset : source_offset + WIDTH]
        source_offset += WIDTH
        decoded = bytearray(WIDTH)
        for column, value in enumerate(encoded):
            left = decoded[column - 1] if column != 0 else 0
            above = previous[column]
            upper_left = previous[column - 1] if column != 0 else 0
            if filter_type == 0:
                result = value
            elif filter_type == 1:
                result = value + left
            elif filter_type == 2:
                result = value + above
            elif filter_type == 3:
                result = value + ((left + above) >> 1)
            elif filter_type == 4:
                result = value + paeth(left, above, upper_left)
            else:
                raise ValueError(f"unsupported PNG filter {filter_type}")
            decoded[column] = result & 0xFF
        pixels[destination_offset : destination_offset + WIDTH] = decoded
        destination_offset += WIDTH
        previous = decoded

    highest_index = max(pixels)
    if highest_index >= len(palette):
        raise ValueError("pixel references a missing PNG palette entry")
    if highest_index >= SOURCE_PALETTE_LIMIT:
        raise ValueError("pixel occupies a status-reserved palette entry")
    return bytes(pixels), palette


def build_bgra_palette(source: list[tuple[int, int, int]]) -> bytes:
    output = bytearray(PALETTE_BYTES)
    for index, (red, green, blue) in enumerate(source):
        output[index * 4 : index * 4 + 4] = bytes((blue, green, red, 0xFF))
    for index, (red, green, blue) in enumerate(
            STATUS_COLORS_RGB, start=SOURCE_PALETTE_LIMIT):
        output[index * 4 : index * 4 + 4] = bytes((blue, green, red, 0xFF))
    return bytes(output)


def build_rescue_font(path: Path) -> bytes:
    source = bytes(int(token, 16) for token in path.read_text().split())
    expected = FONT_GLYPHS * FONT_SOURCE_ROWS
    if len(source) != expected:
        raise ValueError(f"rescue font must contain exactly {expected} bytes")
    output = bytearray(FONT_BYTES)
    for glyph in range(FONT_GLYPHS):
        for row in range(FONT_SOURCE_ROWS):
            value = source[glyph * FONT_SOURCE_ROWS + row]
            destination = glyph * FONT_ROWS + row * 2
            output[destination] = value
            output[destination + 1] = value
    return bytes(output)


def build_payload(image: Path, font: Path) -> bytes:
    pixels, palette = decode_indexed_png(image)
    payload = pixels + build_bgra_palette(palette) + build_rescue_font(font)
    if len(payload) != RAW_BYTES:
        raise AssertionError("internal splash payload-size mismatch")
    return payload


def compress_legacy_lz4(payload: bytes, output: Path) -> bytes:
    executable = shutil.which("lz4")
    if executable is None:
        raise RuntimeError("lz4 command is required to build ROM payloads")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="astra68-splash-") as directory:
        raw_path = Path(directory) / "splash.pal8"
        compressed_path = Path(directory) / "splash.pal8.lz4"
        verified_path = Path(directory) / "verified.pal8"
        raw_path.write_bytes(payload)
        subprocess.run(
            [executable, "-l", "-9", "-f", str(raw_path), str(compressed_path)],
            check=True,
        )
        subprocess.run(
            [executable, "-d", "-f", str(compressed_path), str(verified_path)],
            check=True,
        )
        if verified_path.read_bytes() != payload:
            raise RuntimeError("lz4 verification did not reproduce the payload")
        compressed = compressed_path.read_bytes()
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(compressed)
    temporary.replace(output)
    return compressed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--raw-output", type=Path)
    args = parser.parse_args()

    payload = build_payload(args.image, args.font)
    compressed = compress_legacy_lz4(payload, args.output)
    if args.raw_output is not None:
        args.raw_output.write_bytes(payload)
    print(
        f"{args.output}: {len(compressed)} bytes; raw={len(payload)}; "
        f"raw_crc32={zlib.crc32(payload) & 0xffffffff:08x}; "
        f"sha256={hashlib.sha256(compressed).hexdigest()}"
    )


if __name__ == "__main__":
    main()
