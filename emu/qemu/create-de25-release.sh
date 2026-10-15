#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
OUTPUT=$1
QEMU=${ASTRA_DE25_QEMU:?set ASTRA_DE25_QEMU}
ROM=${ASTRA_DE25_ROM:?set ASTRA_DE25_ROM}
STORAGE=${ASTRA_DE25_STORAGE:?set ASTRA_DE25_STORAGE}
LIBDIR=${ASTRA_DE25_QEMU_LIBDIR:?set ASTRA_DE25_QEMU_LIBDIR}
SOURCE_MANIFEST=${ASTRA_DE25_SOURCE_MANIFEST:?set ASTRA_DE25_SOURCE_MANIFEST}
DISPLAY=$REPOSITORY/build/de25-graphics/linux/astra-terminal-display
AUDIO=$REPOSITORY/build/de25-graphics/linux/astra-audio-host
AUDIO_UNIT=$REPOSITORY/fpga/de25/astra-audio-host.service
RUNTIME_UNIT=$REPOSITORY/fpga/de25/astra.service
REMOTE_DESKTOP=$REPOSITORY/build/de25-capture/astra-remote-desktop
REMOTE_DESKTOP_UNIT=$REPOSITORY/fpga/de25/astra-remote-desktop.service
DE25_SYSROOT=${ASTRA_DE25_SYSROOT:?set ASTRA_DE25_SYSROOT}
KERNEL_PACKAGE=${ASTRA_DE25_KERNEL_PACKAGE:?set ASTRA_DE25_KERNEL_PACKAGE to the complete qualified kernel package}
PROVIDER=$REPOSITORY/build/de25-linux/astra-storage-aarch64
RELEASE_TOOL=$REPOSITORY/tools/astra_release.py
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')

# POST and panic are rendered by this small host binary. Always rebuild it
# from the mirrored tree so a prior release cannot silently supply its font
# metrics or generated assets.
make -B -j "$JOBS" -C "$REPOSITORY/fpga/arty/linux" \
    PLATFORM=de25 CROSS_COMPILE=aarch64-linux-gnu- \
    ../../../build/de25-graphics/linux/astra-terminal-display \
    ../../../build/de25-graphics/linux/astra-audio-host
make -B -j "$JOBS" -C "$REPOSITORY/fpga/de25/linux" \
    CROSS_COMPILE=aarch64-linux-gnu- DE25_SYSROOT="$DE25_SYSROOT" \
    remote-desktop
make -B -j "$JOBS" -C "$REPOSITORY/fpga/de25/linux" \
    CROSS_COMPILE=aarch64-linux-gnu- DE25_SYSROOT="$DE25_SYSROOT" \
    OUT="$REPOSITORY/build/de25-linux" \
    storage-provider-target

# Image, DTBs, all standard modules and Astra modules form one immutable input.
# Never package an isolated module for a different running kernel.
(cd "$KERNEL_PACKAGE" && sha256sum -c --quiet SHA256SUMS)
test -s "$KERNEL_PACKAGE/boot/Image"
test -s "$KERNEL_PACKAGE/metadata/kernel.release"
kernel_release=$(cat "$KERNEL_PACKAGE/metadata/kernel.release")
test -f "$KERNEL_PACKAGE/lib/modules/$kernel_release/kernel/drivers/scsi/sr_mod.ko"
test -f "$KERNEL_PACKAGE/lib/modules/$kernel_release/extra/astra/astra_graphics_arena.ko"
test -f "$KERNEL_PACKAGE/boot/dtbs/intel/socfpga_agilex5_de25_nano.dtb"

# Keep the canonical fabric boot sequence. Only Image/DTB paths change to an
# immutable, package-specific directory; the selected script is installed last.
BOOT_WORKSPACE=$(mktemp -d "${TMPDIR:-/tmp}/astra-kernel-boot.XXXXXX")
trap 'rm -rf "$BOOT_WORKSPACE"' EXIT HUP INT TERM
python3 - "$REPOSITORY/fpga/de25/boot.cmd" "$KERNEL_PACKAGE/SHA256SUMS" "$BOOT_WORKSPACE" <<'PY'
import hashlib
from pathlib import Path
import sys

source, manifest, workspace = map(Path, sys.argv[1:])
identity = hashlib.sha256(manifest.read_bytes()).hexdigest()
script = source.read_text()
for filename in ("Image", "socfpga_agilex5_de25_nano.dtb"):
    old = "} " + filename + ";"
    if script.count(old) != 1:
        raise RuntimeError("canonical DE25 boot path changed: " + filename)
    script = script.replace(old, "} astra-kernels/" + identity + "/" + filename + ";")
(workspace / "boot.cmd").write_text(script)
(workspace / "boot.identity").write_text(identity + "\n")
PY
cp "$REPOSITORY/fpga/de25/boot.its" "$BOOT_WORKSPACE/boot.its"
(cd "$BOOT_WORKSPACE" && SOURCE_DATE_EPOCH=0 mkimage -f boot.its boot.scr.uimg)
dumpimage -l "$BOOT_WORKSPACE/boot.scr.uimg" | grep -Fq 'Type:         Script'

# Astra's shared SoundFont set with its licences and default list. The
# confined storage provider installs it in the SOUND volume, where the guest
# sees it as SOUND:soundfonts and the audio host reads it in place.
SOUNDFONTS=$REPOSITORY/sw/userspace/services/media/build/soundfonts
set --
for path in "$SOUNDFONTS"/*; do
    set -- "$@" "soundfonts/${path##*/}=$path"
done
while read -r checksum relative; do
    set -- "$@" "kernel/package/$relative=$KERNEL_PACKAGE/$relative"
done < "$KERNEL_PACKAGE/SHA256SUMS"
set -- "$@" "kernel/package/SHA256SUMS=$KERNEL_PACKAGE/SHA256SUMS"
for library in libblkid.so.1 libmount.so.1 libudev.so.1 libselinux.so.1 libpcre2-8.so.0; do
    set -- "$@" "qemu/lib/$library=$(readlink -f "$DE25_SYSROOT/usr/lib/aarch64-linux-gnu/$library")"
done

PYTHONDONTWRITEBYTECODE=1 python3 "$RELEASE_TOOL" create "$OUTPUT" \
    "qemu/bin/qemu-system-m68k-astra=$QEMU" \
    "qemu/lib/libpixman-1.so.0=$(readlink -f "$LIBDIR/libpixman-1.so.0")" \
    "qemu/lib/libpcre.so.3=$(readlink -f "$LIBDIR/libpcre.so.3")" \
    "qemu/lib/libglib-2.0.so.0=$(readlink -f "$LIBDIR/libglib-2.0.so.0")" \
    "rom/astra_boot.bin=$ROM" \
    "storage-terminal.img=$STORAGE" \
    "source/SOURCE_SHA256SUMS=$SOURCE_MANIFEST" \
    "bin/astra-terminal-display=$DISPLAY" \
    "bin/astra-audio-host=$AUDIO" \
    "bin/astra-storage=$PROVIDER" \
    "bin/astra-storage-image.py=$SCRIPT_DIR/storage_image.py" \
    "bin/astra-kernel-install.py=$REPOSITORY/fpga/de25/linux/kernel/install-production-storage.py" \
    "kernel/boot.scr.uimg=$BOOT_WORKSPACE/boot.scr.uimg" \
    "kernel/boot.identity=$BOOT_WORKSPACE/boot.identity" \
    "systemd/astra.service=$RUNTIME_UNIT" \
    "systemd/astra-audio-host.service=$AUDIO_UNIT" \
    "bin/astra-remote-desktop=$REMOTE_DESKTOP" \
    "systemd/astra-remote-desktop.service=$REMOTE_DESKTOP_UNIT" \
    "udev/99-astra-qemu.rules=$REPOSITORY/fpga/de25/99-astra-qemu.rules" \
    "bin/astra-input-hotplug.py=$SCRIPT_DIR/astra-input-hotplug.py" \
    "bin/run-arty.sh=$SCRIPT_DIR/run-arty.sh" \
    "bin/runtime-supervisor.py=$SCRIPT_DIR/runtime-supervisor.py" \
    "bin/astra-top=$SCRIPT_DIR/astra-top.py" \
    "bin/de25-ab.sh=$SCRIPT_DIR/de25-ab.sh" \
    "bin/de25-rfb.py=$SCRIPT_DIR/de25-rfb.py" \
    "bin/fs-baseline.py=$SCRIPT_DIR/fs-baseline.py" \
    "bin/astra-release.py=$RELEASE_TOOL" \
    "$@"
