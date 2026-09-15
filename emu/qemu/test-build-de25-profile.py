#!/usr/bin/env python3
"""Keep the DE25 QEMU build on the shared cross-build path."""

from pathlib import Path


build = Path(__file__).with_name("build.sh").read_text(encoding="utf-8")
prepare = Path(__file__).with_name("prepare-source.sh").read_text(encoding="utf-8")

for required in (
    "de25|de25-profile",
    "aarch64-linux-gnu-",
    "BUILD_CONTRACT",
    "DE25_SYSROOT",
    "PKG_CONFIG_SYSROOT_DIR",
    "--sysroot=$DE25_SYSROOT",
    "-nostdinc",
    "-print-file-name=include",
    "-I$COMPILER_INCLUDE",
    "/usr/lib/aarch64-linux-gnu/pkgconfig",
    "-mcpu=cortex-a76",
    'ninja -C "$BUILD" -j "$ASTRA_QEMU_JOBS" qemu-system-m68k >&2',
    'ninja -C "$BUILD" qemu-system-m68k >&2',
):
    assert required in build, f"missing DE25 build contract: {required}"
assert "-mcpu=cortex-a55" not in build
assert build.count("--disable-werror >&2") == 3, \
    "configure output must not contaminate build.sh's stdout artifact path"
assert '--extra-ldflags="$EXTRA_LDFLAGS" >&2' in build
assert 'PUBLIC_DISPLAY_CAPTURE="$REPOSITORY/sw/include/astra/display_capture.h"' in prepare
assert '"sw/include/astra/display_capture.h"' in prepare
assert '"$STAGED_SOURCE/include/astra/display_capture.h"' in prepare

print("DE25 QEMU build profile test: PASS")
