#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
frotz="$root/third_party/frotz/src"
astraz="$root/sw/userspace/ports/astraz"
output=$1
mkdir -p "$output"

cc -std=c99 -D_DEFAULT_SOURCE \
    -I"$frotz/common" -I"$frotz/blorb" -I"$astraz/include" \
    -I"$root/sw/include" -Dmain=astraz_frotz_main \
    -c "$frotz/common/main.c" -o "$output/frotz-main.o"

cc -std=c99 -D_DEFAULT_SOURCE -DASTRAZ_FROTZ_TRANSCRIPT=1 \
    -I"$frotz/common" -I"$frotz/blorb" -I"$astraz/include" \
    -I"$root/sw/include" \
    "$astraz/src/engine.c" "$root/emu/qemu/astraz-media/host_trace.c" \
    "$frotz/blorb/blorblib.c" \
    "$frotz/common/buffer.c" "$frotz/common/err.c" \
    "$frotz/common/fastmem.c" "$frotz/common/files.c" \
    "$frotz/common/getopt.c" "$frotz/common/hotkey.c" \
    "$frotz/common/input.c" "$frotz/common/math.c" \
    "$frotz/common/missing.c" "$frotz/common/object.c" \
    "$frotz/common/process.c" "$frotz/common/quetzal.c" \
    "$frotz/common/random.c" "$frotz/common/redirect.c" \
    "$frotz/common/screen.c" "$frotz/common/sound.c" \
    "$frotz/common/stream.c" "$frotz/common/table.c" \
    "$frotz/common/text.c" "$frotz/common/variable.c" \
    "$output/frotz-main.o" -lm -o "$output/astraz-host-trace"
