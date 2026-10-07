#!/bin/bash
# Regression test for tools/astra-qemu-root's path validation
# (allowed_path(), used for -bios/-qmp/-drive, and the equivalent IMAGE
# checks): a symlink at an expected name, or a file this caller does not
# own, must be refused before QEMU -- running as root -- ever opens it.
#
# Every call passes a real, caller-owned IMAGE/MOUNT pair and a real beast
# QEMU build (so the wrapper gets past both earlier checks and actually
# reaches the ROM path check under test), and a bogus, non-ext4 IMAGE so a
# legitimate ROM path still dies -- at the `mount` line, never having run
# QEMU -- rather than actually booting anything. What distinguishes a
# refusal of the ROM path *under test* from that expected mount failure is
# the die message: a rejected ROM path says "ROM path ...", nothing else
# does. Linux-only (loop mount, sudo); run on beast, never on the Mac.
#
#   bash tools/tests/test_astra_qemu_root_paths.sh [/path/to/astra-qemu-root]
#
# Defaults to the repository copy, so this can run before (or after)
# `sudo install` puts it at /usr/local/sbin/astra-qemu-root.
set -euo pipefail

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
WRAPPER=${1:-$HERE/../astra-qemu-root}
# One level directly under /tmp, like tempfile.mkdtemp(dir="/tmp"): what
# private_tmp_dir() actually accepts (it refuses a nested /tmp/*/*).
WORK=$(mktemp -d /tmp/astra-qemu-root-test.XXXXXX)
chmod 700 "$WORK"
IMAGE="$WORK/image.img"
MOUNT="$WORK/mountdir"
echo image >"$IMAGE"
mkdir -- "$MOUNT"
REPO=$(CDPATH='' cd -- "$HERE/../.." && pwd)
ESCAPE_LINK="$REPO/tools/astra-qemu-root-test-escape-link"
ROOT_OWNED="$REPO/tools/astra-qemu-root-test-root-owned.bin"
# A glob, not `ls | head`: under `pipefail`, `ls` can die of SIGPIPE when
# `head` stops reading early, and that failure would abort this script.
QEMU_CANDIDATES=("$HOME"/.cache/astra68/qemu-9.2.4/build-host-*/qemu-system-m68k)
QEMU=${QEMU_CANDIDATES[0]:-}
[ -n "$QEMU" ] && [ -e "$QEMU" ] || {
    echo "no beast QEMU host build under ~/.cache/astra68/qemu-9.2.4;" \
        "build one with emu/qemu/build.sh host first" >&2
    exit 2
}
PASS=0
FAIL=0

cleanup() {
    rm -f "$ESCAPE_LINK"
    sudo -n rm -f "$ROOT_OWNED" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

# Runs the wrapper with a real image/mount/QEMU and the given ROM path,
# and reports whether its *ROM path check* -- not the `mount` failure
# every one of these calls also hits, since IMAGE is not a real
# filesystem -- is what refused it.
rom_path_refused() {
    local output
    output=$(sudo -n "$WRAPPER" --image "$IMAGE" --mount "$MOUNT" \
        -- "$QEMU" -bios "$1" 2>&1 </dev/null) && return 1
    [[ "$output" == *"ROM path"* ]]
}

expect_refused() {
    local name=$1 path=$2
    if rom_path_refused "$path"; then
        echo "PASS $name (refused: ROM path)"
        PASS=$((PASS + 1))
    else
        echo "FAIL $name: expected the ROM path check to refuse $path"
        FAIL=$((FAIL + 1))
    fi
}

expect_accepted() {
    local name=$1 path=$2
    if rom_path_refused "$path"; then
        echo "FAIL $name: ROM path check refused a legitimate path: $path"
        FAIL=$((FAIL + 1))
    else
        echo "PASS $name (ROM path check did not refuse it)"
        PASS=$((PASS + 1))
    fi
}

echo payload >"$WORK/real-rom.bin"

echo "-- 1. a symlink to /etc/shadow in a private tmp dir --"
ln -s /etc/shadow "$WORK/shadow-link"
expect_refused "symlink-to-shadow-in-private-tmp" "$WORK/shadow-link"

echo "-- 2. a symlink in the repo tree pointing outside it --"
ln -s /etc/shadow "$ESCAPE_LINK"
expect_refused "symlink-in-repo-pointing-outside" "$ESCAPE_LINK"
rm -f "$ESCAPE_LINK"

echo "-- 3. a file owned by another user (root) in the repo tree --"
sudo -n install -o root -g root -m 644 "$WORK/real-rom.bin" "$ROOT_OWNED"
expect_refused "file-owned-by-another-user" "$ROOT_OWNED"
sudo -n rm -f "$ROOT_OWNED"

echo "-- 4. control: a legitimate caller-owned path under the repo --"
expect_accepted "legitimate-owned-path-under-repo" "$REPO/tools/astra-qemu-root"

echo "-- 5. control: a legitimate caller-owned path in a private tmp dir --"
expect_accepted "legitimate-owned-path-in-private-tmp" "$WORK/real-rom.bin"

echo "ASTRA-QEMU-ROOT-PATH-TEST passed $PASS failed $FAIL"
[ "$FAIL" -eq 0 ]
