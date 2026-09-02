#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
OUTPUT=$1
QEMU=${ASTRA_ARTY_QEMU:?set ASTRA_ARTY_QEMU}
ROM=${ASTRA_ARTY_ROM:?set ASTRA_ARTY_ROM}
STORAGE=${ASTRA_ARTY_STORAGE:?set ASTRA_ARTY_STORAGE}
DISPLAY=${ASTRA_ARTY_TERMINAL_DISPLAY:?set ASTRA_ARTY_TERMINAL_DISPLAY}
LIBDIR=${ASTRA_ARTY_QEMU_LIBDIR:?set ASTRA_ARTY_QEMU_LIBDIR}
RELEASE_TOOL=$REPOSITORY/tools/astra_release.py
PIXMAN=$(readlink -f "$LIBDIR/libpixman-1.so.0")
PCRE=$(readlink -f "$LIBDIR/libpcre2-8.so.0")
GLIB=$(readlink -f "$LIBDIR/libglib-2.0.so.0")
GMODULE=$(readlink -f "$LIBDIR/libgmodule-2.0.so.0")

PYTHONDONTWRITEBYTECODE=1 python3 "$RELEASE_TOOL" create "$OUTPUT" \
    "qemu/bin/qemu-system-m68k-astra=$QEMU" \
    "qemu/lib/libpixman-1.so.0=$PIXMAN" \
    "qemu/lib/libpcre2-8.so.0=$PCRE" \
    "qemu/lib/libglib-2.0.so.0=$GLIB" \
    "qemu/lib/libgmodule-2.0.so.0=$GMODULE" \
    "rom/astra_boot.bin=$ROM" \
    "storage-terminal.img=$STORAGE" \
    "bin/astra-terminal-display=$DISPLAY" \
    "bin/astra-input-hotplug.py=$SCRIPT_DIR/astra-input-hotplug.py" \
    "bin/run-arty.sh=$SCRIPT_DIR/run-arty.sh" \
    "bin/astra-release.py=$RELEASE_TOOL"
