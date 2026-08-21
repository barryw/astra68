#!/usr/bin/env python3
"""Small check for the provider index built into system images."""

import os
import struct
import tempfile

import astra_image


with tempfile.TemporaryDirectory() as directory:
    bundles = []
    for kit, version in (("Old.kit", "1.2.0"), ("New.kit", "1.10.0")):
        bundle = os.path.join(directory, kit)
        os.mkdir(bundle)
        with open(os.path.join(bundle, "manifest"), "w", encoding="ascii") as manifest:
            manifest.write("kind kit\nprovides filesystem.library 1 %s\n" % version)
        library = os.path.join(bundle, "libraries", "filesystem.library",
                               "abi-1", version, "m68k-68030")
        os.makedirs(library)
        with open(os.path.join(library, "filesystem.library"), "wb") as image:
            identity = struct.pack(
                ">IHHHHHHHHIII24s32s40s", 0x414c4942, 1, 128,
                *(int(part) for part in version.split(".")), 1, 0, 0,
                0x4d303330, 0x12345678, 0x00f00000,
                b"filesystem.library", b"test", b"test")
            image.write(bytes(0x200))
            image.write(identity)
        bundles.append(bundle)

    providers = astra_image._providers(bundles)
    assert providers[("filesystem.library", 1)] == (
        (1, 10, 0), 0, 0x12345678,
        "LIBS:New.kit/libraries/filesystem.library/abi-1/1.10.0/"
        "m68k-68030/filesystem.library")

print("astra image provider index: PASS")
