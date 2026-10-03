#!/bin/bash
# Phase 0 of docs/USERSPACE_FPU.md: time MC68040 FPU instructions against
# Astra's soft-float helpers on the DE25's Cortex-A76, under Astra's QEMU.
#
# Run on beast. Builds toolchain/bench-fpu-ops.c three ways -- hard float,
# soft float against Astra's libgcc (the helpers compiler.library ships),
# soft float against the distribution m68k libgcc when it has the helpers --
# builds qemu-m68k user mode from the Astra QEMU source for AArch64, copies
# both to the board and prints host seconds per kernel.
#
# usage: toolchain/bench-fpu-ops.sh [BOARD]   (default root@192.168.1.52)
#
# The DE25 has two Cortex-A55s (CPUs 0-1) and two A76s (2-3); Astra's vCPU
# runs on CPU 3. The benchmark is pinned to BENCH_CPU (default 3), so stop
# astra.service first or the two share a core. Unpinned, it lands on an A55
# and runs about ten times slower.
#
# User mode leaves out the softmmu TLB lookup on every guest load and store,
# which the soft helpers make more of than the FPU does, so it understates
# the soft-float cost slightly.
source ~/astra-mg/build-lock.sh
set -euo pipefail
BOARD=${1:-root@192.168.1.52}
BENCH_CPU=${BENCH_CPU:-3}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$HOME/astra-mg/bench-fpu
mkdir -p "$WORK"

CC=m68k-linux-gnu-gcc
CFLAGS='-m68040 -O2 -static -nostdlib -no-pie -fno-pic -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns'
ASTRA_LIBGCC=$(m68k-astra-gcc -print-libgcc-file-name)
DISTRO_LIBGCC=$($CC -msoft-float -print-libgcc-file-name)
SRC=$ROOT/toolchain/bench-fpu-ops.c
$CC $CFLAGS -o "$WORK/bench-hard" "$SRC" -lgcc
$CC $CFLAGS -msoft-float -o "$WORK/bench-soft-astra" "$SRC" "$ASTRA_LIBGCC"
VARIANTS="hard soft-astra"
if $CC $CFLAGS -msoft-float -o "$WORK/bench-soft-distro" "$SRC" "$DISTRO_LIBGCC" 2>/dev/null; then
    VARIANTS="$VARIANTS soft-distro"
fi
if [[ $(m68k-linux-gnu-nm "$WORK/bench-hard") == *__mulsf3* ]]; then
    echo "hard-float build calls __mulsf3" >&2
    exit 1
fi
if [[ $(m68k-linux-gnu-nm "$WORK/bench-soft-astra") != *" T __mulsf3"* ]]; then
    echo "soft-float build has no __mulsf3" >&2
    exit 1
fi

export ASTRA_QEMU_WORK_ROOT=${ASTRA_QEMU_WORK_ROOT:-$HOME/.cache/astra68/qemu-9.2.4}
SOURCE=$("$ROOT/emu/qemu/prepare-source.sh")
SYSROOT=${DE25_SYSROOT:-$HOME/.cache/astra68/de25-jammy-arm64}
BUILD=$ASTRA_QEMU_WORK_ROOT/build-de25-user-${SOURCE##*source-}
if [ ! -f "$BUILD/build.ninja" ]; then
    mkdir -p "$BUILD"
    INCLUDE=$(aarch64-linux-gnu-gcc -print-file-name=include)
    (cd "$BUILD" && env PKG_CONFIG_SYSROOT_DIR="$SYSROOT" \
        PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig:$SYSROOT/usr/share/pkgconfig" \
        PKG_CONFIG_PATH= \
        "$SOURCE/configure" --target-list=m68k-linux-user \
        --cross-prefix=aarch64-linux-gnu- --cpu=aarch64 \
        --without-default-features --enable-tcg --enable-lto \
        --enable-linux-user --disable-system --disable-debug-info --disable-werror \
        --extra-cflags="--sysroot=$SYSROOT -nostdinc -I$INCLUDE -isystem $SYSROOT/usr/include/aarch64-linux-gnu -isystem $SYSROOT/usr/include -mcpu=cortex-a76 -O3 -fomit-frame-pointer" \
        --extra-ldflags="--sysroot=$SYSROOT") >"$WORK/configure.log" 2>&1
fi
ninja -C "$BUILD" qemu-m68k >"$WORK/ninja.log" 2>&1

ssh "$BOARD" mkdir -p /data/bench-fpu
scp -q "$BUILD/qemu-m68k" "$WORK"/bench-* "$BOARD":/data/bench-fpu/
ssh "$BOARD" taskset -c "$BENCH_CPU" python3 - $VARIANTS <<'PY'
import subprocess, sys, time
variants = sys.argv[1:]
rounds = {"pan": 400, "muladd": 400, "div": 200, "dmuladd": 200, "int": 400}

def best(variant, kernel, count):
    """Fastest of three runs: host seconds and the program's checksum."""
    times = []
    for _ in range(3):
        start = time.perf_counter()
        out = subprocess.run(["/data/bench-fpu/qemu-m68k", "-cpu", "m68040",
                              f"/data/bench-fpu/bench-{variant}", kernel, str(count)],
                             check=True, capture_output=True, text=True).stdout
        times.append(time.perf_counter() - start)
    return min(times), out.split()[1]

# Each run pays QEMU's start-up and the program's setup; take it off.
startup = {v: best(v, "int", 0)[0] for v in variants}
print(f"host ns per element, start-up {startup['hard'] * 1e3:.0f} ms subtracted")
print(f"{'kernel':8} {'elements':>9} " + " ".join(f"{v:>12}" for v in variants) + "  soft/hard")
failed = False
for kernel, count in rounds.items():
    results = {v: best(v, kernel, count) for v in variants}
    ns = {v: (t - startup[v]) * 1e9 / (count * 4096) for v, (t, _) in results.items()}
    # The FPU and Astra's helpers both round correctly, so they must agree
    # bit for bit; the distribution's lb1sf68 helpers do not round correctly.
    agree = results["hard"][1] == results["soft-astra"][1]
    failed |= not agree
    print(f"{kernel:8} {count * 4096:9} " + " ".join(f"{ns[v]:12.1f}" for v in variants)
          + f"  {ns['soft-astra'] / ns['hard']:9.2f}" + ("" if agree else "  RESULTS DIFFER"))
sys.exit(1 if failed else 0)
PY
