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
DISPLAY=$REPOSITORY/build/de25-graphics/linux/astra-terminal-display
RELEASE_TOOL=$REPOSITORY/tools/astra_release.py
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')

# POST and panic are rendered by this small host binary. Always rebuild it
# from the mirrored tree so a prior release cannot silently supply its font
# metrics or generated assets.
make -B -j "$JOBS" -C "$REPOSITORY/fpga/arty/linux" \
    PLATFORM=de25 CROSS_COMPILE=aarch64-linux-gnu- \
    ../../../build/de25-graphics/linux/astra-terminal-display

PYTHONDONTWRITEBYTECODE=1 python3 "$RELEASE_TOOL" create "$OUTPUT" \
    "qemu/bin/qemu-system-m68k-astra=$QEMU" \
    "qemu/lib/libpixman-1.so.0=$(readlink -f "$LIBDIR/libpixman-1.so.0")" \
    "qemu/lib/libpcre.so.3=$(readlink -f "$LIBDIR/libpcre.so.3")" \
    "qemu/lib/libglib-2.0.so.0=$(readlink -f "$LIBDIR/libglib-2.0.so.0")" \
    "rom/astra_boot.bin=$ROM" \
    "storage-terminal.img=$STORAGE" \
    "bin/astra-terminal-display=$DISPLAY" \
    "bin/astra-input-hotplug.py=$SCRIPT_DIR/astra-input-hotplug.py" \
    "bin/run-arty.sh=$SCRIPT_DIR/run-arty.sh" \
    "bin/astra-release.py=$RELEASE_TOOL"
