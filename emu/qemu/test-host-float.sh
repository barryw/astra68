#!/bin/bash
# Gate for qemu-9.2/target-m68k-host-float.patch: the host-float fast path
# must be invisible to the guest. Builds qemu-m68k user mode twice from the
# same prepared source -- as prepared, and with that one patch reversed --
# and runs toolchain/bench-fpu-ops.c's `flags` kernel under both. The kernel
# folds every result image (extended, single, double) and FPSR of the
# operations the patch touches, over random and edge operands, in every
# rounding mode and precision. The checksums must be identical.
#
# Run on beast. usage: emu/qemu/test-host-float.sh [PAIRS] [BOARD]
# With BOARD (e.g. root@192.168.1.52), also runs the board's AArch64 build
# that toolchain/bench-fpu-ops.sh deployed to /data/bench-fpu, on an A76.
source ~/astra-mg/build-lock.sh
set -euo pipefail
PAIRS=${1:-2000000}
BOARD=${2:-}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export ASTRA_QEMU_WORK_ROOT=${ASTRA_QEMU_WORK_ROOT:-$HOME/.cache/astra68/qemu-9.2.4}
SOURCE=$("$ROOT/emu/qemu/prepare-source.sh")
ID=${SOURCE##*source-}
WORK=$HOME/astra-mg/host-float
mkdir -p "$WORK"

REFERENCE=$ASTRA_QEMU_WORK_ROOT/reference-host-float-$ID
if [ ! -d "$REFERENCE" ]; then
    rm -rf "$REFERENCE.tmp"
    cp -a "$SOURCE" "$REFERENCE.tmp"
    patch -d "$REFERENCE.tmp" -p1 -R \
        < "$ROOT/emu/qemu/qemu-9.2/target-m68k-host-float.patch" >/dev/null
    mv "$REFERENCE.tmp" "$REFERENCE"
fi

build() {  # SOURCE BUILD
    if [ ! -f "$2/build.ninja" ]; then
        mkdir -p "$2"
        (cd "$2" && "$1/configure" --target-list=m68k-linux-user \
            --without-default-features --enable-tcg --enable-linux-user \
            --disable-system --disable-werror) >"$2.configure.log" 2>&1
    fi
    ninja -C "$2" qemu-m68k >"$2.ninja.log" 2>&1
}
PATCHED=$ASTRA_QEMU_WORK_ROOT/build-host-user-$ID
UNPATCHED=$ASTRA_QEMU_WORK_ROOT/build-host-user-reference-$ID
build "$SOURCE" "$PATCHED"
build "$REFERENCE" "$UNPATCHED"

m68k-linux-gnu-gcc -m68040 -O2 -static -nostdlib -no-pie -fno-pic \
    -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
    -o "$WORK/bench-hard" "$ROOT/toolchain/bench-fpu-ops.c" -lgcc

want=$("$UNPATCHED/qemu-m68k" -cpu m68040 "$WORK/bench-hard" flags "$PAIRS")
got=$("$PATCHED/qemu-m68k" -cpu m68040 "$WORK/bench-hard" flags "$PAIRS")
echo "floatx80 only:  $want"
echo "host float x86: $got"
status=0
[ "$got" = "$want" ] || status=1
if [ -n "$BOARD" ]; then
    scp -q "$WORK/bench-hard" "$BOARD:/data/bench-fpu/bench-hard-flags"
    board=$(ssh "$BOARD" taskset -c "${BENCH_CPU:-3}" \
        /data/bench-fpu/qemu-m68k -cpu m68040 \
        /data/bench-fpu/bench-hard-flags flags "$PAIRS")
    echo "host float A76: $board"
    [ "$board" = "$want" ] || status=1
fi
if [ "$status" -ne 0 ]; then
    echo "FAIL: the host-float path changed what the guest sees"
    exit 1
fi
echo "PASS: host-float path is invisible to the guest ($PAIRS pairs)"
