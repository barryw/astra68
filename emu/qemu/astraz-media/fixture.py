#!/usr/bin/env python3
"""Generate freely redistributable Z6/Blorb media for AstraZ gates.

The story executes draw_picture(7, 1, 1), sound_effect(3, 2), then quits.
The companion uses standard Blorb resource indexing; no game assets are used.
"""

import argparse
import math
from pathlib import Path
import struct
import zlib


def be16(value):
    return struct.pack(">H", value)


def be32(value):
    return struct.pack(">I", value)


def chunk(tag, payload):
    return tag + be32(len(payload)) + payload + bytes(len(payload) & 1)


def png():
    def png_chunk(tag, payload):
        return (be32(len(payload)) + tag + payload +
                be32(zlib.crc32(tag + payload)))

    # Bright halves are large enough to verify in an HDMI/RFB frame.
    pixels = b"".join(b"\0" +
                      (b"\xff\0\xff\xff" if y < 32 else
                       b"\0\xff\xff\xff") * 64
                      for y in range(64))
    return (b"\x89PNG\r\n\x1a\n" +
            png_chunk(b"IHDR", be32(64) + be32(64) + b"\x08\x06\0\0\0") +
            png_chunk(b"IDAT", zlib.compress(pixels)) +
            png_chunk(b"IEND", b""))


def aiff():
    # One second of signed 8-bit mono 440 Hz at 8000 Hz. The sample rate is
    # encoded as an IEEE extended 80-bit number.
    frames = 8000
    samples = bytes(int(64 * math.sin(2 * math.pi * 440 * i / 8000)) & 0xff
                    for i in range(frames))
    comm = be16(1) + be32(frames) + be16(8) + bytes.fromhex("400bfa00000000000000")
    return b"AIFF" + chunk(b"COMM", comm) + \
        chunk(b"SSND", be32(0) + be32(0) + samples)


def mod():
    # Small valid four-channel ProTracker module, one C-3 note and one pattern.
    header = bytearray(1084)
    header[:20] = b"AstraZ media test"
    header[20:42] = b"AstraZ sample".ljust(22, b"\0")
    header[42:44] = be16(16)  # sample length is counted in 16-bit words
    header[45] = 64           # sample volume
    header[950] = 1           # song has one pattern
    header[951] = 0x7f
    header[1080:1084] = b"M.K."
    pattern = bytearray(1024)
    pattern[:4] = bytes.fromhex("01 ac 10 00")  # sample 1, C-3 period 428
    sample = bytes((i * 8 - 128) & 0xff for i in range(32))
    return bytes(header + pattern + sample)


def story():
    data = bytearray(256)
    data[0] = 6
    data[2:4] = be16(1)       # release
    data[4:6] = be16(0x80)    # high memory
    data[6:8] = be16(0x20)    # main routine at 0x80, packed by four
    data[8:10] = be16(0x40)   # dictionary
    data[10:12] = be16(0x80)  # objects
    data[12:14] = be16(0x60)  # globals
    data[14:16] = be16(0x80)  # static memory
    data[16:18] = be16(0x88)  # story requests graphics and sound
    data[18:24] = b"261029"
    data[24:26] = be16(0x70)  # abbreviations
    data[26:28] = be16(len(data) // 8)
    data[0x80] = 0            # main routine: no locals
    data[0x81:0x8c] = bytes.fromhex(
        "be 05 57 07 01 01"  # EXT:5 draw_picture 7,1,1
        "f5 5f 03 02"        # VAR:15 sound_effect 3,2
        "ba"                 # 0OP:10 quit
    )
    data[28:30] = be16(sum(data[64:]) & 0xffff)
    return bytes(data)


def blorb(sound_tag, sound_payload):
    resources = ((b"Pict", 7, b"PNG ", png()),
                 (b"Snd ", 3, sound_tag, sound_payload))
    index_length = 4 + 12 * len(resources)
    offset = 12 + 8 + index_length
    index = bytearray(be32(len(resources)))
    payloads = bytearray()
    for usage, number, tag, payload in resources:
        index += usage + be32(number) + be32(offset)
        encoded = chunk(tag, payload)
        payloads += encoded
        offset += len(encoded)
    contents = b"IFRS" + chunk(b"RIdx", bytes(index)) + payloads
    return b"FORM" + be32(len(contents)) + contents


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    aiff_data = aiff()
    (args.directory / "media.z6").write_bytes(story())
    (args.directory / "media.png").write_bytes(png())
    (args.directory / "media.aiff").write_bytes(b"FORM" +
                                                 be32(len(aiff_data)) + aiff_data)
    (args.directory / "media.blb").write_bytes(blorb(b"FORM", aiff_data))
    (args.directory / "media.mod").write_bytes(mod())
    (args.directory / "media-mod.blb").write_bytes(
        blorb(b"MOD ", mod()))
    print("AstraZ synthetic Z6/Blorb fixtures: PASS")


if __name__ == "__main__":
    main()
