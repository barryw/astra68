#!/usr/bin/env python3
"""Pin the DE25 release to its AArch64 runtime dependencies."""

from pathlib import Path


script = Path(__file__).with_name("create-de25-release.sh").read_text()
for required in (
    "ASTRA_DE25_QEMU",
    "ASTRA_DE25_ROM",
    "ASTRA_DE25_STORAGE",
    "ASTRA_DE25_TERMINAL_DISPLAY",
    "ASTRA_DE25_QEMU_LIBDIR",
    "qemu/lib/libpixman-1.so.0=",
    "qemu/lib/libpcre.so.3=",
    "qemu/lib/libglib-2.0.so.0=",
    'python3 "$RELEASE_TOOL" create',
):
    assert required in script, required

print("DE25 release creation contract: PASS")
