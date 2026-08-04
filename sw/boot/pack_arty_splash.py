#!/usr/bin/env python3
"""Convert the fixed 1280x720 Astra splash PNG to big-endian RGB565."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import tempfile
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
WIDTH = 1280
HEIGHT = 720
PITCH = WIDTH * 2
OUTPUT_BYTES = PITCH * HEIGHT


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


def decode_png(path: Path) -> tuple[bytes, int]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("splash source is not a PNG")

    offset = len(PNG_SIGNATURE)
    width = height = bit_depth = color_type = interlace = None
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
        elif chunk_type == b"IDAT":
            compressed.extend(payload)
        elif chunk_type == b"IEND":
            saw_end = True
            break
        offset = crc_end

    if not saw_end:
        raise ValueError("PNG has no IEND")
    if width != WIDTH or height != HEIGHT:
        raise ValueError(f"splash must be exactly {WIDTH}x{HEIGHT}")
    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError(
            "splash must be non-interlaced 8-bit RGB or RGBA PNG")

    channels = 4 if color_type == 6 else 3
    row_bytes = WIDTH * channels
    filtered = zlib.decompress(bytes(compressed))
    if len(filtered) != HEIGHT * (row_bytes + 1):
        raise ValueError("unexpected PNG scanline size")

    pixels = bytearray(HEIGHT * row_bytes)
    previous = bytearray(row_bytes)
    source_offset = 0
    destination_offset = 0
    for _ in range(HEIGHT):
        filter_type = filtered[source_offset]
        source_offset += 1
        encoded = filtered[source_offset : source_offset + row_bytes]
        source_offset += row_bytes
        decoded = bytearray(row_bytes)
        for column, value in enumerate(encoded):
            left = decoded[column - channels] if column >= channels else 0
            above = previous[column]
            upper_left = previous[column - channels] \
                if column >= channels else 0
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
        pixels[destination_offset : destination_offset + row_bytes] = decoded
        destination_offset += row_bytes
        previous = decoded
    return bytes(pixels), channels


def quantize(value: int, maximum: int) -> int:
    return (value * maximum + 127) // 255


def convert_rgb565(pixels: bytes, channels: int) -> bytes:
    output = bytearray(OUTPUT_BYTES)
    source = 0
    destination = 0
    for _ in range(WIDTH * HEIGHT):
        red = pixels[source]
        green = pixels[source + 1]
        blue = pixels[source + 2]
        if channels == 4:
            alpha = pixels[source + 3]
            red = (red * alpha + 127) // 255
            green = (green * alpha + 127) // 255
            blue = (blue * alpha + 127) // 255
        value = (quantize(red, 31) << 11) | \
                (quantize(green, 63) << 5) | \
                quantize(blue, 31)
        output[destination] = value >> 8
        output[destination + 1] = value & 0xFF
        source += channels
        destination += 2
    return bytes(output)


def write_preview(path: Path, packed: bytes) -> None:
    preview = bytearray(WIDTH * HEIGHT * 3)
    destination = 0
    for source in range(0, len(packed), 2):
        value = (packed[source] << 8) | packed[source + 1]
        red = (value >> 11) & 0x1F
        green = (value >> 5) & 0x3F
        blue = value & 0x1F
        preview[destination] = (red << 3) | (red >> 2)
        preview[destination + 1] = (green << 2) | (green >> 4)
        preview[destination + 2] = (blue << 3) | (blue >> 2)
        destination += 3
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii") + preview)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    pixels, channels = decode_png(args.image)
    packed = convert_rgb565(pixels, channels)
    if len(packed) != OUTPUT_BYTES:
        raise AssertionError("internal RGB565 payload-size mismatch")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
            prefix=args.output.name + ".", dir=args.output.parent,
            delete=False) as temporary:
        temporary.write(packed)
        temporary_path = Path(temporary.name)
    temporary_path.replace(args.output)
    if args.preview is not None:
        write_preview(args.preview, packed)
    print(
        f"{args.output}: {len(packed)} bytes; {WIDTH}x{HEIGHT}; "
        f"pitch={PITCH}; crc32={zlib.crc32(packed) & 0xffffffff:08x}; "
        f"sha256={hashlib.sha256(packed).hexdigest()}"
    )


if __name__ == "__main__":
    main()
