#!/usr/bin/env python3
"""Build Astra's deterministic multi-strike Terminal icon."""

import struct
import sys

MAGIC = 0x4149434F
SIZES = (16, 32, 64)
PALETTE = (
    (0, 0, 0, 0),          # transparent
    (3, 6, 9, 255),        # frame
    (48, 63, 72, 255),     # title
    (9, 16, 23, 255),      # terminal void
    (45, 174, 184, 255),   # ion cyan
    (236, 239, 240, 255),  # text
    (124, 139, 148, 255),  # quiet detail
)


def terminal_strike(size):
    """A designed strike, not a scaled source image."""
    pixels = bytearray(size * size)
    border = max(1, size // 32)
    radius = max(2, size // 8)
    title = max(3, size // 5)

    for y in range(size):
        for x in range(size):
            corner = ((x < radius and y < radius and
                       (x - radius) ** 2 + (y - radius) ** 2 > radius ** 2) or
                      (x >= size - radius and y < radius and
                       (x - (size - radius - 1)) ** 2 +
                       (y - radius) ** 2 > radius ** 2) or
                      (x < radius and y >= size - radius and
                       (x - radius) ** 2 +
                       (y - (size - radius - 1)) ** 2 > radius ** 2) or
                      (x >= size - radius and y >= size - radius and
                       (x - (size - radius - 1)) ** 2 +
                       (y - (size - radius - 1)) ** 2 > radius ** 2))
            if corner:
                value = 0
            elif (x < border or y < border or x >= size - border or
                  y >= size - border):
                value = 1
            elif y < title:
                value = 2
            else:
                value = 3
            pixels[y * size + x] = value

    # Astra signal rail under the titlebar.
    for y in range(title, min(size - border, title + border)):
        for x in range(border, size - border):
            pixels[y * size + x] = 4

    # Prompt chevron and underline cursor; thickness is strike-specific.
    thick = max(1, size // 32)
    x0 = max(3, size // 5)
    y0 = title + max(3, size // 5)
    glyph = max(3, size // 6)
    for at in range(glyph):
        for step in range(thick):
            for y in (y0 + at, y0 + glyph * 2 - 2 - at):
                if 0 <= y < size:
                    pixels[y * size + x0 + at // 2 + step] = 5
    cursor_x = x0 + glyph
    cursor_y = min(size - border - thick - 1, y0 + glyph * 2)
    for y in range(cursor_y, cursor_y + thick):
        for x in range(cursor_x, min(size - border, cursor_x + glyph * 2)):
            pixels[y * size + x] = 4
    return bytes(pixels)


def build():
    strikes = [(size, terminal_strike(size)) for size in SIZES]
    palette_offset = 32
    strike_offset = palette_offset + len(PALETTE) * 4
    data_offset = strike_offset + len(strikes) * 16
    cursor = data_offset
    records = []
    payload = bytearray()
    for size, pixels in strikes:
        records.append(struct.pack(">HHIII", size, size, cursor,
                                   len(pixels), 0))
        payload.extend(pixels)
        cursor += len(pixels)
    header = struct.pack(">IHHIHHIIII", MAGIC, 1, 32, cursor,
                         len(strikes), len(PALETTE), palette_offset,
                         strike_offset, data_offset, 0)
    palette = b"".join(bytes(entry) for entry in PALETTE)
    return header + palette + b"".join(records) + payload


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: aicon.py OUTPUT.aicon")
    with open(sys.argv[1], "wb") as handle:
        handle.write(build())


if __name__ == "__main__":
    main()
