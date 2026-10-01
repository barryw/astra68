#!/usr/bin/env python3
import os
import pathlib
import struct
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
import aicon


def strikes(encoded):
    count = struct.unpack_from(">H", encoded, 12)[0]
    offset = struct.unpack_from(">I", encoded, 20)[0]
    result = []
    for index in range(count):
        width, height, data, length, _ = struct.unpack_from(
            ">HHIII", encoded, offset + index * 16)
        result.append((width, height, encoded[data:data + length]))
    return result


def bmp(width, height, pixels, bits=24, alpha=False):
    """A bottom-up Windows bitmap; pixels[y][x] = (r, g, b[, a])."""
    table = b""
    if bits == 8:
        colors = sorted({pixel[:3] for row in pixels for pixel in row})
        index = {color: at for at, color in enumerate(colors)}
        table = b"".join(bytes((b, g, r, 0)) for r, g, b in colors)
    stride = ((width * bits + 31) // 32) * 4
    body = bytearray()
    for row in reversed(pixels):
        line = bytearray()
        for pixel in row:
            if bits == 8:
                line.append(index[pixel[:3]])
            elif bits == 24:
                line += bytes((pixel[2], pixel[1], pixel[0]))
            else:
                line += bytes((pixel[2], pixel[1], pixel[0], pixel[3]))
        body += line + bytes(stride - len(line))
    header = 40 if not alpha else 56
    offset = 14 + header + len(table)
    info = struct.pack("<IiiHHIIiiII", header, width, height, 1, bits,
                       3 if alpha else 0, len(body), 2835, 2835,
                       len(table) // 4, 0)
    if alpha:
        info += struct.pack("<IIII", 0x00ff0000, 0x0000ff00, 0x000000ff,
                            0xff000000)
    return (b"BM" + struct.pack("<IHHI", offset + len(body), 0, 0, offset) +
            info + table + bytes(body))


def build_image(data):
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "art.bmp")
        with open(path, "wb") as handle:
            handle.write(data)
        return aicon.build(path)


def palette_count(encoded):
    return struct.unpack_from(">H", encoded, 14)[0]


def image_icons():
    white, red, blue = (255, 255, 255), (255, 0, 0), (0, 0, 255)
    # Colour key: the top-left colour is transparent; red and blue survive.
    art = [[white] * 4, [white, red, blue, white], [white, blue, red, white],
           [white] * 4]
    encoded = build_image(bmp(4, 4, art))
    assert palette_count(encoded) == 3
    for width, height, pixels in strikes(encoded):
        assert pixels[0] == 0 and pixels[width * height // 2 + width // 2]
        assert set(pixels) == {0, 1, 2}
    # Alpha when the bitmap has it; no colour key then.
    art = [[(10, 20, 30, 0), (10, 20, 30, 255)]] * 2
    encoded = build_image(bmp(2, 2, art, bits=32, alpha=True))
    for width, height, pixels in strikes(encoded):
        assert pixels[0] == 0 and pixels[width - 1] == 1
    # A photograph is reduced to the colour budget.
    art = [[(x * 16, y * 16, 128) for x in range(16)] for y in range(16)]
    encoded = build_image(bmp(16, 16, art, bits=8))
    assert palette_count(encoded) == aicon.IMAGE_COLORS_MAX + 1
    # A wide image keeps its aspect, centred vertically.
    art = [[(0, 0, 0), red, red, red]]
    encoded = build_image(bmp(4, 1, art))
    for width, height, pixels in strikes(encoded):
        assert all(value == 0 for value in pixels[:width * (height // 3)])
    try:
        build_image(b"not a bitmap")
    except ValueError:
        pass
    else:
        raise AssertionError("non-bitmap accepted")


def wad(lumps):
    """An IWAD holding the given (name, bytes) lumps."""
    body = bytearray()
    entries = []
    for name, data in lumps:
        entries.append((12 + len(body), len(data), name))
        body += data
    directory = 12 + len(body)
    return (b"IWAD" + struct.pack("<II", len(lumps), directory) + bytes(body) +
            b"".join(struct.pack("<II8s", offset, size, name.encode())
                     for offset, size, name in entries))


def wad_icons():
    # PLAYPAL: index i is (i, 255 - i, 64). A 4x6 face: column x is one post
    # of palette index 10 * (x + 1), rows 1..4; rows 0 and 5 transparent.
    playpal = b"".join(bytes((i, 255 - i, 64)) for i in range(256)) * 14
    columns = [bytes((1, 4, 0)) + bytes([10 * (x + 1)] * 4) + bytes((0, 255))
               for x in range(4)]
    offsets, at = [], 8 + 4 * 4
    for column in columns:
        offsets.append(at)
        at += len(column)
    face = (struct.pack("<HHhh", 4, 6, 0, 0) +
            b"".join(struct.pack("<I", offset) for offset in offsets) +
            b"".join(columns))
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "doom1.wad")
        with open(path, "wb") as handle:
            handle.write(wad([("PLAYPAL", playpal), ("STFST01", face)]))
        encoded = aicon.build(path)
        with open(path, "wb") as handle:
            handle.write(wad([("PLAYPAL", playpal)]))
        try:
            aicon.build(path)
        except ValueError:
            pass
        else:
            raise AssertionError("WAD without the face accepted")
    # Tile void, ember ring, then the face's own four colours.
    assert palette_count(encoded) == 3 + 4
    for width, height, pixels in strikes(encoded):
        middle = (height // 2) * width
        assert pixels[0] == 0 and pixels[-1] == 0          # rounded corner
        assert pixels[width // 2] == 2                     # ember ring
        assert pixels[middle + 2] == 1                     # void beside face
        assert set(pixels[middle:middle + width]) >= {3, 4, 5, 6}


def main():
    image_icons()
    wad_icons()
    for width, height, pixels in strikes(aicon.build("terminal")):
        assert width == height and len(pixels) == width * height
        assert 3 in pixels and 4 in pixels and 5 in pixels
        assert 2 not in pixels  # No titlebar band.
        assert all(sum(pixel == 4 for pixel in
                       pixels[y * width:(y + 1) * width]) < width // 2
                   for y in range(height))  # No signal rail masquerading as UI.
        assert pixels[0] == 0 and pixels[-1] == 0
    try:
        aicon.build("missing")
    except ValueError:
        pass
    else:
        raise AssertionError("unknown icon kind accepted")
    print("aicon tests passed")


if __name__ == "__main__":
    main()
