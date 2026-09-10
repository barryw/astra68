#!/usr/bin/env python3
"""Pin the DE25 release to its AArch64 runtime dependencies."""

from pathlib import Path


script = Path(__file__).with_name("create-de25-release.sh").read_text()
for required in (
    "ASTRA_DE25_QEMU",
    "ASTRA_DE25_ROM",
    "ASTRA_DE25_STORAGE",
    "ASTRA_DE25_QEMU_LIBDIR",
    "build/de25-graphics/linux/astra-terminal-display",
    'make -B -j "$JOBS" -C "$REPOSITORY/fpga/arty/linux"',
    "getconf _NPROCESSORS_ONLN",
    "qemu/lib/libpixman-1.so.0=",
    "qemu/lib/libpcre.so.3=",
    "qemu/lib/libglib-2.0.so.0=",
    'python3 "$RELEASE_TOOL" create',
):
    assert required in script, required
assert "ASTRA_DE25_TERMINAL_DISPLAY" not in script

graphics_makefile = (Path(__file__).parents[2] /
                     "fpga/arty/linux/Makefile").read_text()
assert "$(NDK_INCLUDE)/astra/theme.h" in graphics_makefile

print("DE25 release creation contract: PASS")
