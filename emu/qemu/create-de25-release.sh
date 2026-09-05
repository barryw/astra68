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
DISPLAY=${ASTRA_DE25_TERMINAL_DISPLAY:?set ASTRA_DE25_TERMINAL_DISPLAY}
LIBDIR=${ASTRA_DE25_QEMU_LIBDIR:?set ASTRA_DE25_QEMU_LIBDIR}
RELEASE_TOOL=$REPOSITORY/tools/astra_release.py

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
