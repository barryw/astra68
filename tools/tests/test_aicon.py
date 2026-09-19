#!/usr/bin/env python3
import pathlib
import struct
import sys

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


def main():
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
