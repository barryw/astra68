#!/usr/bin/env python3
"""System selection refuses ambiguity, out-of-file windows and symlinks."""
from pathlib import Path
import struct
import tempfile

from storage_image import ext4_partition

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    image = root / "image"
    header = bytearray(512)
    header[510:512] = b"\x55\xaa"
    header[446:462] = struct.pack("<B3sB3sII", 0, bytes(3), 0x83, bytes(3), 2, 4)
    image.write_bytes(header + bytes(4096 - 512))
    assert ext4_partition(image) == (1024, 2048)

    def refused(value):
        image.write_bytes(value)
        try:
            ext4_partition(image)
            raise AssertionError("invalid system selection accepted")
        except ValueError:
            pass

    bad = header.copy()
    bad[462:478] = bad[446:462]
    refused(bad + bytes(4096 - 512))
    bad = header.copy()
    struct.pack_into("<II", bad, 454, 2, 100)
    refused(bad + bytes(4096 - 512))
    refused(header)
    refused(bytes(4096))
    link = root / "link"
    link.symlink_to(image)
    try:
        ext4_partition(link)
        raise AssertionError("symlink system image accepted")
    except OSError:
        pass
print("Exact system image window: PASS")
