#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
OUTPUT=$1
RELEASE_TOOL=$REPOSITORY/tools/astra_release.py
SUPERVISOR_ELF=${ASTRA_BENCH_SUPERVISOR_ELF:-$REPOSITORY/sw/userspace/supervisor/build/m68k/astra_supervisor.elf}
TERMINAL_ELF=${ASTRA_BENCH_TERMINAL_ELF:-$REPOSITORY/sw/userspace/services/terminal/build/m68k/terminal.elf}

PYTHONDONTWRITEBYTECODE=1 python3 "$RELEASE_TOOL" create "$OUTPUT" \
    "bin/astra-release.py=$RELEASE_TOOL" \
    "emu/qemu/astra-input-hotplug.py=$SCRIPT_DIR/astra-input-hotplug.py" \
    "emu/qemu/astra_image.py=$SCRIPT_DIR/astra_image.py" \
    "emu/qemu/event_catalog.py=$REPOSITORY/tools/event_catalog.py" \
    "emu/qemu/test-filesystem-stress.py=$SCRIPT_DIR/test-filesystem-stress.py" \
    "emu/qemu/test-host-channel-userspace.py=$SCRIPT_DIR/test-host-channel-userspace.py" \
    "emu/qemu/test-terminal.py=$SCRIPT_DIR/test-terminal.py" \
    "emu/qemu/trace_decode.py=$REPOSITORY/tools/trace_decode.py" \
    "sw/kernel/trace.h=$REPOSITORY/sw/kernel/trace.h" \
    "sw/userspace/services/terminal/build/m68k/terminal.elf=$TERMINAL_ELF" \
    "sw/userspace/supervisor/build/m68k/astra_supervisor.elf=$SUPERVISOR_ELF"
