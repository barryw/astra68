#!/usr/bin/env python3
"""Exact system-volume selection shared by image tools and service launchers."""

import argparse
import os
import stat
import struct
import subprocess


def _header(image):
    fd = os.open(image, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise ValueError("system image must be a regular file")
        return os.pread(fd, 2048, 0), info.st_size
    finally:
        os.close(fd)


def ext4_partition(image):
    header, size = _header(image)
    if len(header) < 512 or header[510:512] != b"\x55\xaa":
        raise ValueError("system image requires a valid MBR")
    entries = [header[446 + i * 16:462 + i * 16] for i in range(4)]
    selected = [entry for entry in entries if entry[4] == 0x83]
    if len(selected) != 1:
        raise ValueError("exactly one system partition required")
    start, sectors = struct.unpack_from("<II", selected[0], 8)
    offset, length = start * 512, sectors * 512
    if length == 0 or offset < 512 or offset > size or length > size - offset:
        raise ValueError("invalid system partition window")
    return offset, length


def image_window(image):
    """Astra MBR image or a bare ext4 test volume, plus its exact UUID."""
    header, size = _header(image)
    if len(header) >= 1082 and header[1080:1082] == b"\x53\xef":
        offset, length = 0, size
    else:
        offset, length = ext4_partition(image)
    uuid = subprocess.check_output([
        "blkid", "-p", "-O", str(offset), "-S", str(length),
        "-s", "UUID", "-o", "value", str(image)], text=True).strip()
    if not uuid or "\n" in uuid:
        raise ValueError("exact system ext4 UUID required")
    return offset, length, uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    args = parser.parse_args()
    offset, length, uuid = image_window(args.image)
    print(offset, length, uuid)


if __name__ == "__main__":
    main()
