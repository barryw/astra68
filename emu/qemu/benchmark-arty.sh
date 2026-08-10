#!/bin/sh
set -eu

RUNS="${1:-10}"
ASTRA_ROOT="${ASTRA_ROOT:-/data/astra}"
QEMU="${QEMU:-$ASTRA_ROOT/qemu/bin/qemu-system-m68k-astra}"
ROM="${ROM:-$ASTRA_ROOT/rom/astra_boot.bin}"
LIBDIR="${LIBDIR:-$ASTRA_ROOT/qemu/lib}"
LOGDIR="${LOGDIR:-$ASTRA_ROOT/log}"
TIMINGS="$LOGDIR/qemu-arty-timings.txt"
MARKERS="$LOGDIR/qemu-arty-markers.txt"
RUN_ERR="$LOGDIR/qemu-arty-run.err"

mkdir -p "$LOGDIR"
: > "$TIMINGS"
: > "$MARKERS"

i=1
while [ "$i" -le "$RUNS" ]; do
    : > "$RUN_ERR"
    /usr/bin/time -f "$i %e %U %S" -a -o "$TIMINGS" \
        env LD_LIBRARY_PATH="$LIBDIR" \
        "$QEMU" \
        -object memory-backend-ram,id=astra-ram,size=128M,prealloc=on \
        -M astra68,memory-backend=astra-ram -m 128M -bios "$ROM" \
        -nographic -monitor none -serial none -no-reboot \
        -icount shift=8,align=off,sleep=off \
        >/dev/null 2>"$RUN_ERR"
    if ! grep -q "ASTRA68-QEMU READY" "$RUN_ERR"; then
        echo "run $i did not reach READY" >&2
        cat "$RUN_ERR" >&2
        exit 1
    fi
    cat "$RUN_ERR" >> "$MARKERS"
    i=$((i + 1))
done

cat "$TIMINGS"
cat "$MARKERS"
