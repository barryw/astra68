#!/usr/bin/env python3
"""Pin the DE25 release to its AArch64 runtime dependencies."""

from pathlib import Path


script = Path(__file__).with_name("create-de25-release.sh").read_text()
for required in (
    "ASTRA_DE25_QEMU",
    "ASTRA_DE25_ROM",
    "ASTRA_DE25_STORAGE",
    "ASTRA_DE25_QEMU_LIBDIR",
    "ASTRA_DE25_SOURCE_MANIFEST",
    "ASTRA_DE25_SYSROOT",
    "build/de25-graphics/linux/astra-terminal-display",
    'make -B -j "$JOBS" -C "$REPOSITORY/fpga/arty/linux"',
    'make -B -j "$JOBS" -C "$REPOSITORY/fpga/de25/linux"',
    "bin/astra-remote-desktop=",
    "bin/astra-audio-host=",
    "systemd/astra-remote-desktop.service=",
    "systemd/astra-audio-host.service=",
    "systemd/astra.service=",
    "getconf _NPROCESSORS_ONLN",
    "qemu/lib/libpixman-1.so.0=",
    "qemu/lib/libpcre.so.3=",
    "qemu/lib/libglib-2.0.so.0=",
    "source/SOURCE_SHA256SUMS=",
    'python3 "$RELEASE_TOOL" create',
):
    assert required in script, required
assert "ASTRA_DE25_TERMINAL_DISPLAY" not in script

graphics_makefile = (Path(__file__).parents[2] /
                     "fpga/arty/linux/Makefile").read_text()
assert "$(NDK_INCLUDE)/astra/theme.h" in graphics_makefile

remote_unit = (Path(__file__).parents[2] /
               "fpga/de25/astra-remote-desktop.service").read_text()
assert "Before=astra.service" in remote_unit
assert "WantedBy=astra.service" in remote_unit
assert "RuntimeDirectory=astra" in remote_unit
assert "RuntimeDirectoryPreserve=yes" in remote_unit
assert "After=astra.service" not in remote_unit
assert "Requires=astra.service" not in remote_unit

print("DE25 release creation contract: PASS")
