#!/usr/bin/env python3
"""Keep the DE25 QEMU build on the shared cross-build path."""

from pathlib import Path
import subprocess
import tempfile


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
    'TARGETS="$TARGETS contrib-plugins"',
    'ninja -C "$BUILD" -j "$ASTRA_QEMU_JOBS" $TARGETS >&2',
    'ninja -C "$BUILD" $TARGETS >&2',
):
    assert required in build, f"missing DE25 build contract: {required}"
assert "-mcpu=cortex-a55" not in build
assert build.count("--disable-werror >&2") == 3, \
    "configure output must not contaminate build.sh's stdout artifact path"
assert '--extra-ldflags="$EXTRA_LDFLAGS" >&2' in build
assert 'PUBLIC_ADDRESS_SPACE="$REPOSITORY/sw/include/astra/address_space.h"' in prepare
assert '"sw/include/astra/address_space.h"' in prepare
assert '"$STAGED_SOURCE/include/astra/address_space.h"' in prepare
assert 'PUBLIC_DISPLAY_CAPTURE="$REPOSITORY/sw/include/astra/display_capture.h"' in prepare
assert '"sw/include/astra/display_capture.h"' in prepare
assert '"$STAGED_SOURCE/include/astra/display_capture.h"' in prepare

patch_file = Path(__file__).with_name("qemu-9.2") / "meson.build.patch"
hw_meson = """m68k_ss = ss.source_set()

m68k_ss.add(when: 'CONFIG_AN5206', if_true: files('an5206.c', 'mcf5206.c'))
m68k_ss.add(when: 'CONFIG_MCF5208', if_true: files('mcf5208.c', 'mcf_intc.c'))
m68k_ss.add(when: 'CONFIG_NEXTCUBE', if_true: files('next-kbd.c', 'next-cube.c'))
m68k_ss.add(when: 'CONFIG_Q800', if_true: files('q800.c', 'q800-glue.c'))
m68k_ss.add(when: 'CONFIG_M68K_VIRT', if_true: files('virt.c'))

hw_arch += {'m68k': m68k_ss}
"""
plugin_meson = """contrib_plugins = ['bbv', 'cache', 'cflow', 'drcov', 'execlog', 'hotblocks',
                   'hotpages', 'howvec', 'hwprofile', 'ips', 'stoptrigger']
if host_os != 'windows'
  # lockstep uses socket.h
  contrib_plugins += 'lockstep'
endif
"""


def apply_overlay(plugin_source):
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary)
        (source / "hw/m68k").mkdir(parents=True)
        (source / "contrib/plugins").mkdir(parents=True)
        (source / "hw/m68k/meson.build").write_text(hw_meson)
        (source / "contrib/plugins/meson.build").write_text(plugin_source)
        result = subprocess.run(
            ["patch", "-d", str(source), "-p1", "--forward", "--batch",
             "-i", str(patch_file)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        output = (source / "contrib/plugins/meson.build").read_text()
        return result.returncode, output


status, patched = apply_overlay(plugin_meson)
assert status == 0 and "['astra_profile', 'bbv'" in patched, \
    "QEMU overlay must apply to the pinned source"
status, _ = apply_overlay(plugin_meson.replace("'bbv'", "'changed'", 1))
assert status != 0, "QEMU overlay must reject an unexpected upstream source"

print("DE25 QEMU build profile test: PASS")
