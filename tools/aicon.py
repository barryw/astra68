#!/usr/bin/env python3
"""Build Astra's deterministic, designed multi-strike application icons."""

import struct
import sys

MAGIC = 0x4149434F
SIZES = (16, 32, 64)
TERMINAL_PALETTE = (
    (0, 0, 0, 0),          # transparent
    (3, 6, 9, 255),        # frame
    (48, 63, 72, 255),     # title
    (9, 16, 23, 255),      # terminal void
    (45, 174, 184, 255),   # ion cyan
    (236, 239, 240, 255),  # text
    (124, 139, 148, 255),  # quiet detail
)

GALLERY_PALETTE = (
    (0, 0, 0, 0),          # transparent
    (3, 6, 9, 255),        # frame
    (48, 63, 72, 255),     # title
    (236, 239, 240, 255),  # lunar client
    (70, 79, 86, 255),     # graphite control
    (45, 174, 184, 255),   # ion cyan
    (177, 73, 73, 255),    # fault
)


def terminal_strike(size):
    """A simple terminal symbol with no window toolbar."""
    pixels = bytearray(size * size)
    border = max(1, size // 32)
    left = size // 8
    right = size - left - 1
    top = size // 6
    bottom = size - top - 1
    radius = max(2, size // 12)

    def inside(x, y, inset=0):
        x0, x1 = left + inset, right - inset
        y0, y1 = top + inset, bottom - inset
        r = max(0, radius - inset)

        if x < x0 or x > x1 or y < y0 or y > y1:
            return False
        if r == 0:
            return True
        cx = x0 + r if x < x0 + r else x1 - r if x > x1 - r else x
        cy = y0 + r if y < y0 + r else y1 - r if y > y1 - r else y
        return (x - cx) ** 2 + (y - cy) ** 2 <= r ** 2

    for y in range(size):
        for x in range(size):
            if inside(x, y):
                pixels[y * size + x] = 3 if inside(x, y, border) else 5

    def line(x0, y0, x1, y1, color):
        steps = max(abs(x1 - x0), abs(y1 - y0), 1)
        thick = max(1, size // 32)

        for step in range(steps + 1):
            x = round(x0 + (x1 - x0) * step / steps)
            y = round(y0 + (y1 - y0) * step / steps)
            for dy in range(thick):
                for dx in range(thick):
                    pixels[(y + dy) * size + x + dx] = color

    x0 = 3 * size // 10
    point = 17 * size // 40
    y0 = 2 * size // 5
    middle = size // 2
    baseline = 5 * size // 8
    line(x0, y0, point, middle, 4)
    line(point, middle, x0, baseline, 4)
    line(21 * size // 40, baseline, 29 * size // 40, baseline, 4)
    return bytes(pixels)


def gallery_strike(size):
    """A control panel whose detail is redrawn for each required strike."""
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

    for y in range(title, min(size - border, title + border)):
        for x in range(border, size - border):
            pixels[y * size + x] = 5

    margin = max(2, size // 8)
    gap = max(1, size // 16)
    control_height = max(2, size // 7)
    control_width = (size - margin * 2 - gap) // 2
    first_y = title + margin
    for row in range(2):
        y0 = first_y + row * (control_height + gap)
        if y0 + control_height >= size - border:
            break
        for column in range(2):
            x0 = margin + column * (control_width + gap)
            value = 5 if row == 0 and column == 0 else \
                (6 if row == 1 and column == 1 else 4)
            for y in range(y0, y0 + control_height):
                for x in range(x0, x0 + control_width):
                    pixels[y * size + x] = value
    return bytes(pixels)


def build(kind):
    choices = {
        "terminal": (TERMINAL_PALETTE, terminal_strike),
        "gallery": (GALLERY_PALETTE, gallery_strike),
    }
    try:
        palette, builder = choices[kind]
    except KeyError as error:
        raise ValueError("unknown icon kind: %s" % kind) from error
    strikes = [(size, builder(size)) for size in SIZES]
    palette_offset = 32
    strike_offset = palette_offset + len(palette) * 4
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
                         len(strikes), len(palette), palette_offset,
                         strike_offset, data_offset, 0)
    encoded_palette = b"".join(bytes(entry) for entry in palette)
    return header + encoded_palette + b"".join(records) + payload


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: aicon.py {terminal|gallery} OUTPUT.aicon")
    with open(sys.argv[2], "wb") as handle:
        handle.write(build(sys.argv[1]))


if __name__ == "__main__":
    main()
