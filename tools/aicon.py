#!/usr/bin/env python3
"""Build Astra's deterministic, designed multi-strike application icons."""

import struct
import zlib
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

INSTRUMENT_KINDS = ("framebench", "draw", "sprite", "geometry", "target",
                    "scale", "keys", "mouse", "timer", "thread")
OBJECT_KINDS = ("atlas", "volume", "drawer", "document", "fonts")
OBJECT_PALETTE = ((0, 0, 0, 0), (247, 249, 249, 255),
                  (223, 228, 230, 255), (92, 104, 112, 255),
                  (107, 119, 128, 255), (43, 127, 134, 255),
                  (236, 239, 240, 255), (25, 34, 42, 255))


def object_strike(kind, size):
    """Atlas object geometry from the supplied 40-unit desktop design grid."""
    pixels = bytearray(size * size)
    thick = max(1, size // 32)

    def line(x0, y0, x1, y1, color):
        x0, y0, x1, y1 = (round(v * size / 40) for v in (x0, y0, x1, y1))
        steps = max(abs(x1 - x0), abs(y1 - y0), 1)
        for step in range(steps + 1):
            x = round(x0 + (x1 - x0) * step / steps)
            y = round(y0 + (y1 - y0) * step / steps)
            for dy in range(thick):
                for dx in range(thick):
                    if 0 <= x + dx < size and 0 <= y + dy < size:
                        pixels[(y + dy) * size + x + dx] = color

    def polygon(points, fill, stroke):
        for y in range(size):
            for x in range(size):
                px, py = (x + .5) * 40 / size, (y + .5) * 40 / size
                inside = False
                for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1]):
                    if (y0 > py) != (y1 > py) and \
                            px < (x1 - x0) * (py - y0) / (y1 - y0) + x0:
                        inside = not inside
                if inside:
                    pixels[y * size + x] = fill
        for first, last in zip(points, points[1:] + points[:1]):
            line(*first, *last, stroke)

    if kind in ("drawer", "atlas"):
        polygon(((5.5, 11.5), (16.5, 11.5), (20, 16), (34.5, 16),
                 (34.5, 34), (5.5, 34)), 2, 3)
        line(10, 22, 30, 22, 5)
        if kind == "atlas":
            line(20, 25, 20, 31, 5)
            line(17, 28, 23, 28, 5)
    elif kind == "document":
        polygon(((8.5, 4.5), (31.5, 4.5), (31.5, 35.5), (8.5, 35.5)), 1, 3)
        for y in (12, 17, 22):
            line(13, y, 27, y, 4)
        line(13, 27, 21, 27, 5)
    elif kind == "fonts":
        polygon(((8.5, 4.5), (31.5, 4.5), (31.5, 35.5), (8.5, 35.5)), 1, 3)
        line(12, 29, 20, 11, 5)
        line(20, 11, 28, 29, 5)
        line(15, 23, 25, 23, 4)
    elif kind == "volume":
        polygon(((5, 10), (35, 10), (35, 31), (5, 31)), 7, 6)
        polygon(((11, 15), (29, 15), (29, 21), (11, 21)), 7, 6)
        line(19, 27, 29, 27, 6)
        line(10, 27, 12, 27, 5)
    else:
        raise ValueError("unknown object icon: %s" % kind)
    return bytes(pixels)


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


def instrument_strike(kind, size):
    """Lunar outlines and cyan signals, redrawn on each native pixel grid."""
    pixels = bytearray(size * size)
    thick = max(1, size // 32)

    def line(x0, y0, x1, y1, color=5):
        x0, y0, x1, y1 = (round(v * size / 32) for v in (x0, y0, x1, y1))
        steps = max(abs(x1 - x0), abs(y1 - y0), 1)
        for step in range(steps + 1):
            x = round(x0 + (x1 - x0) * step / steps)
            y = round(y0 + (y1 - y0) * step / steps)
            for dy in range(thick):
                for dx in range(thick):
                    pixels[(y + dy) * size + x + dx] = color

    def box(x0, y0, x1, y1, color=5):
        line(x0, y0, x1, y0, color)
        line(x1, y0, x1, y1, color)
        line(x1, y1, x0, y1, color)
        line(x0, y1, x0, y0, color)

    if kind == "framebench":
        box(4, 6, 28, 26)
        line(7, 22, 25, 22, 6)
        for x, top in ((9, 17), (15, 12), (21, 9)):
            line(x, top, x, 21, 4)
            line(x + 1, top, x + 1, 21, 4)
    elif kind == "draw":
        line(7, 24, 22, 9)
        line(10, 27, 25, 12, 4)
        line(22, 9, 25, 12)
        line(7, 24, 6, 28)
        line(6, 28, 10, 27)
        line(6, 8, 11, 8, 6)
        line(6, 8, 6, 13, 6)
    elif kind == "sprite":
        for cx, cy, color in ((10, 19, 6), (22, 19, 5), (16, 10, 4)):
            line(cx, cy - 5, cx + 5, cy, color)
            line(cx + 5, cy, cx, cy + 5, color)
            line(cx, cy + 5, cx - 5, cy, color)
            line(cx - 5, cy, cx, cy - 5, color)
    elif kind == "geometry":
        line(6, 25, 16, 7)
        line(16, 7, 26, 25)
        line(26, 25, 6, 25)
        for x, y in ((6, 25), (16, 7), (26, 25)):
            box(x - 1, y - 1, x + 1, y + 1, 4)
    elif kind == "target":
        box(11, 5, 27, 22, 6)
        box(4, 11, 21, 28)
        box(8, 16, 17, 24, 4)
    elif kind == "scale":
        box(4, 20, 12, 28, 6)
        box(17, 4, 28, 15)
        line(11, 21, 21, 11, 4)
        line(16, 11, 21, 11, 4)
        line(21, 11, 21, 16, 4)
    elif kind == "keys":
        box(3, 9, 29, 24)
        for y in (12, 16):
            for x in (6, 12, 18, 24):
                line(x, y, x + 2, y, 4 if (x, y) == (12, 12) else 6)
        line(10, 21, 23, 21, 4)
    elif kind == "mouse":
        outline = ((11, 5), (21, 5), (25, 9), (25, 23), (21, 27),
                   (11, 27), (7, 23), (7, 9), (11, 5))
        for first, last in zip(outline, outline[1:]):
            line(*first, *last)
        line(7, 14, 25, 14, 6)
        line(16, 5, 16, 13, 4)
    elif kind == "timer":
        outline = ((16, 7), (24, 10), (27, 18), (24, 26), (16, 29),
                   (8, 26), (5, 18), (8, 10), (16, 7))
        for first, last in zip(outline, outline[1:]):
            line(*first, *last)
        line(13, 3, 19, 3, 6)
        line(16, 3, 16, 6, 6)
        line(16, 11, 16, 18, 4)
        line(16, 18, 21, 21, 4)
    elif kind == "thread":
        line(8, 6, 8, 26, 6)
        line(24, 6, 24, 26, 6)
        line(8, 7, 24, 17, 4)
        line(8, 17, 24, 27, 4)
        for x in (8, 24):
            for y in (6, 16, 26):
                box(x - 2, y - 2, x + 2, y + 2)
    else:
        raise ValueError("unknown instrument icon: %s" % kind)
    return bytes(pixels)


# The desktop draws an icon one palette colour at a time into a bounded draw
# list, so an image is reduced to this many opaque colours plus transparency.
IMAGE_COLORS_MAX = 15


def read_bmp(path):
    """Uncompressed 1/4/8/24/32-bit Windows bitmap -> (width, height, rows of
    (r, g, b, a)). Without an alpha channel the top-left pixel's colour is
    transparent, the colour-key convention SDL's own sample art uses."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"BM" or len(data) < 54:
        raise ValueError("%s is not a Windows bitmap" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    header = struct.unpack_from("<I", data, 14)[0]
    width, height, planes, bits, compression = struct.unpack_from(
        "<iiHHI", data, 18)
    colors = struct.unpack_from("<I", data, 46)[0] if header >= 40 else 0
    if planes != 1 or width <= 0 or height == 0 or bits not in (
            1, 4, 8, 24, 32) or compression not in (0, 3):
        raise ValueError("%s: unsupported bitmap layout" % path)
    if compression == 3 and bits != 32:
        raise ValueError("%s: unsupported bitmap masks" % path)
    top_down = height < 0
    height = abs(height)
    table = []
    if bits <= 8:
        count = colors or (1 << bits)
        base = 14 + header
        for index in range(count):
            b, g, r = data[base + index * 4:base + index * 4 + 3]
            table.append((r, g, b, 255))
    stride = ((width * bits + 31) // 32) * 4
    rows = []
    for row in range(height):
        source = row if top_down else height - 1 - row
        line = data[offset + source * stride:offset + (source + 1) * stride]
        pixels = []
        for x in range(width):
            if bits == 32:
                b, g, r, a = line[x * 4:x * 4 + 4]
                pixels.append((r, g, b, a if compression == 3 else 255))
            elif bits == 24:
                b, g, r = line[x * 3:x * 3 + 3]
                pixels.append((r, g, b, 255))
            else:
                bit = x * bits
                value = (line[bit // 8] >> (8 - bits - bit % 8)) & \
                    ((1 << bits) - 1)
                pixels.append(table[value])
        rows.append(pixels)
    if all(pixel[3] == 255 for line in rows for pixel in line):
        key = rows[0][0]
        rows = [[(0, 0, 0, 0) if pixel == key else pixel for pixel in line]
                for line in rows]
    return width, height, rows


def read_png(path):
    """Non-interlaced 8-bit PNG (grey, RGB, palette, grey+alpha, RGBA) ->
    (width, height, rows of (r, g, b, a)): the icons upstream programs ship."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    at, header, palette, alpha, compressed = 8, None, [], b"", bytearray()
    while at + 8 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, length, 3)]
        elif kind == b"tRNS":
            alpha = body
        elif kind == b"IDAT":
            compressed.extend(body)
        elif kind == b"IEND":
            break
        at += 12 + length
    if header is None:
        raise ValueError("%s has no PNG header" % path)
    width, height, depth, color, _, _, interlace = header
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if depth != 8 or channels is None or interlace != 0:
        raise ValueError("%s: unsupported PNG layout" % path)
    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    previous = bytearray(stride)
    rows = []
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - channels] if x >= channels else 0
            up = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + left) & 255
            elif kind == 2:
                line[x] = (line[x] + up) & 255
            elif kind == 3:
                line[x] = (line[x] + (left + up) // 2) & 255
            elif kind == 4:
                guess = left + up - corner
                near = min((abs(guess - left), 0), (abs(guess - up), 1),
                           (abs(guess - corner), 2))[1]
                line[x] = (line[x] + (left, up, corner)[near]) & 255
            elif kind != 0:
                raise ValueError("%s: bad PNG filter %d" % (path, kind))
        previous = line
        pixels = []
        for x in range(width):
            value = line[x * channels:(x + 1) * channels]
            if color == 0:
                pixels.append((value[0], value[0], value[0], 255))
            elif color == 2:
                pixels.append((value[0], value[1], value[2], 255))
            elif color == 3:
                index = value[0]
                pixels.append(palette[index] +
                              (alpha[index] if index < len(alpha) else 255,))
            elif color == 4:
                pixels.append((value[0], value[0], value[0], value[1]))
            else:
                pixels.append(tuple(value))
        rows.append(pixels)
    return width, height, rows


def image_icon(path):
    """(palette, strike builder) for an image: aspect kept, centred, nearest
    sampled, reduced to IMAGE_COLORS_MAX colours by popularity."""
    width, height, rows = (read_png(path) if path.lower().endswith(".png")
                           else read_bmp(path))
    counts = {}
    for line in rows:
        for pixel in line:
            if pixel[3] >= 128:
                key = pixel[:3]
                counts[key] = counts.get(key, 0) + 1
    if not counts:
        raise ValueError("%s has no opaque pixels" % path)
    chosen = sorted(counts, key=lambda key: (-counts[key], key))
    chosen = chosen[:IMAGE_COLORS_MAX]
    palette = ((0, 0, 0, 0),) + tuple(color + (255,) for color in chosen)

    def nearest(color):
        best = min(range(len(chosen)), key=lambda index: sum(
            (a - b) ** 2 for a, b in zip(chosen[index], color)))
        return best + 1

    mapping = {}

    def strike(size):
        pixels = bytearray(size * size)
        scale = max(width, height)
        drawn_w = max(1, width * size // scale)
        drawn_h = max(1, height * size // scale)
        left = (size - drawn_w) // 2
        top = (size - drawn_h) // 2
        for y in range(drawn_h):
            for x in range(drawn_w):
                pixel = rows[y * height // drawn_h][x * width // drawn_w]
                if pixel[3] < 128:
                    continue
                key = pixel[:3]
                if key not in mapping:
                    mapping[key] = nearest(key)
                pixels[(top + y) * size + left + x] = mapping[key]
        return bytes(pixels)

    return palette, strike


# A game's own art on an Astra tile: the rounded tile of the system icons,
# its ring in ember, the game's picture inside it.
TILE_VOID = (9, 16, 23)
TILE_EMBER = (200, 40, 24)
# The status-bar face looking straight ahead: Doom at any size.
DOOM_FACE = "STFST01"


def read_wad_patch(path, name):
    """A Doom-format WAD's picture lump, coloured by its PLAYPAL ->
    (width, height, rows of (r, g, b, a))."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] not in (b"IWAD", b"PWAD"):
        raise ValueError("%s is not a WAD" % path)
    count, directory = struct.unpack_from("<II", data, 4)
    lumps = {}
    for index in range(count):
        offset, size, lump = struct.unpack_from("<II8s", data,
                                                directory + 16 * index)
        lumps.setdefault(lump.rstrip(b"\0").decode("ascii", "replace"),
                         data[offset:offset + size])
    if "PLAYPAL" not in lumps or name not in lumps:
        raise ValueError("%s has no PLAYPAL or %s" % (path, name))
    palette = [tuple(lumps["PLAYPAL"][i * 3:i * 3 + 3]) for i in range(256)]
    patch = lumps[name]
    width, height = struct.unpack_from("<HH", patch)
    rows = [[(0, 0, 0, 0)] * width for _ in range(height)]
    for x in range(width):
        at = struct.unpack_from("<I", patch, 8 + 4 * x)[0]
        # Each column is posts of (top, length, pad, pixels..., pad).
        while patch[at] != 255:
            top, length = patch[at], patch[at + 1]
            for row in range(length):
                if top + row < height:
                    rows[top + row][x] = palette[patch[at + 3 + row]] + (255,)
            at += 4 + length
    return width, height, rows


def reduce_colors(counts, limit):
    """At most `limit` colours for a picture: k-means over its colours,
    weighted by use and seeded by popularity, so a face keeps its shading
    where popularity alone would keep only its most common tones."""
    items = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
    centers = [color for color, _ in items[:limit]]
    for _ in range(16):
        groups = [[] for _ in centers]
        for color, uses in items:
            groups[min(range(len(centers)), key=lambda index: sum(
                (a - b) ** 2 for a, b in zip(color, centers[index])))
                   ].append((color, uses))
        centers = [tuple(round(sum(color[channel] * uses
                                   for color, uses in group) /
                               sum(uses for _, uses in group))
                         for channel in range(3)) if group else
                   centers[index] for index, group in enumerate(groups)]
    return sorted(set(centers))


def wad_icon(path):
    """(palette, strike builder) for a Doom IWAD: its status-bar face on an
    Astra tile, filling the tile's height."""
    width, height, rows = read_wad_patch(path, DOOM_FACE)
    counts = {}
    for line in rows:
        for pixel in line:
            if pixel[3]:
                counts[pixel[:3]] = counts.get(pixel[:3], 0) + 1
    face = reduce_colors(counts, IMAGE_COLORS_MAX - 2)
    palette = ((0, 0, 0, 0), TILE_VOID + (255,), TILE_EMBER + (255,)) + \
        tuple(color + (255,) for color in face)
    nearest = {color: 3 + min(range(len(face)), key=lambda index: sum(
        (a - b) ** 2 for a, b in zip(color, face[index])))
        for color in counts}

    def strike(size):
        pixels = bytearray(size * size)
        border = max(1, size // 32)
        radius = max(2, size // 6)

        def inside(x, y, inset=0):
            x0, x1 = inset, size - 1 - inset
            r = max(0, radius - inset)
            if x < x0 or x > x1 or y < x0 or y > x1:
                return False
            cx = x0 + r if x < x0 + r else x1 - r if x > x1 - r else x
            cy = x0 + r if y < x0 + r else x1 - r if y > x1 - r else y
            return (x - cx) ** 2 + (y - cy) ** 2 <= r ** 2

        for y in range(size):
            for x in range(size):
                if inside(x, y):
                    pixels[y * size + x] = 1 if inside(x, y, border) else 2
        drawn_h = size - 2 * border - 2 * max(1, size // 16)
        drawn_w = max(1, round(width * drawn_h / height))
        left = (size - drawn_w) // 2
        top = (size - drawn_h) // 2
        for y in range(drawn_h):
            for x in range(drawn_w):
                pixel = rows[y * height // drawn_h][x * width // drawn_w]
                if pixel[3] and inside(left + x, top + y, border):
                    pixels[(top + y) * size + left + x] = nearest[pixel[:3]]
        return bytes(pixels)

    return palette, strike


def build(kind):
    choices = {
        "terminal": (TERMINAL_PALETTE, terminal_strike),
        "gallery": (GALLERY_PALETTE, gallery_strike),
    }
    if kind in choices:
        palette, builder = choices[kind]
    elif kind in INSTRUMENT_KINDS:
        palette = TERMINAL_PALETTE
        builder = lambda size: instrument_strike(kind, size)
    elif kind in OBJECT_KINDS:
        palette = OBJECT_PALETTE
        builder = lambda size: object_strike(kind, size)
    elif kind.lower().endswith((".bmp", ".png")):
        palette, builder = image_icon(kind)
    elif kind.lower().endswith(".wad"):
        palette, builder = wad_icon(kind)
    else:
        raise ValueError("unknown icon kind: %s" % kind)
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
        raise SystemExit("usage: aicon.py {terminal|gallery|" +
                         "|".join(INSTRUMENT_KINDS + OBJECT_KINDS) + "|IMAGE.bmp|"
                         "IMAGE.png|DOOM.wad} "
                         "OUTPUT.aicon")
    with open(sys.argv[2], "wb") as handle:
        handle.write(build(sys.argv[1]))


if __name__ == "__main__":
    main()
