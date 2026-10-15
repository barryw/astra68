#!/bin/sh
# Start the machine stopped with a debugger attached to it.
#
# QEMU has always carried a gdb stub and it was never usable, because nothing
# was built with debug information: gdb attached and answered `?? ()`. Every
# m68k object is compiled with -g now, so this is the whole of what was
# missing -- pointing gdb at the two symbol files, since the kernel and the
# initial image are separate ELFs living at fixed addresses.
#
# The user image's symbols come from the unstripped ELF, not the one in the
# ROM. They describe the same code; only the DWARF is different.
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)

QEMU=${QEMU:?set QEMU to the qualified Beast emulator}
ROM=${ROM:-$ROOT/sw/boot/build/astra_boot.bin}
KERNEL_ELF=${KERNEL_ELF:-$ROOT/sw/kernel/build/astra_kernel.elf}
USER_ELF=${USER_ELF:-$ROOT/sw/userspace/supervisor/build/m68k/astra_supervisor.elf}
USER_TEXT=${USER_TEXT:-0x00100000}
# The ROM is where the machine is at reset, so its symbols are the first ones
# anybody needs.
BOOT_ELF=${BOOT_ELF:-$ROOT/sw/boot/build/astra_boot.elf}
BOOT_TEXT=${BOOT_TEXT:-0xffe00400}
PORT=${PORT:-1234}
IMAGE=${IMAGE:-}
STORAGE_HELPER=/usr/local/sbin/astra-qemu-root
GDB=${GDB:-gdb-multiarch}

usage() {
    cat <<'USAGE'
usage: debug.sh [--image CARD] [--port N] [--no-gdb]

environment: QEMU ROM KERNEL_ELF USER_ELF USER_TEXT BOOT_ELF BOOT_TEXT PORT GDB

Boots the machine stopped at the reset vector with the gdb stub listening,
then attaches gdb with both symbol tables loaded. `continue` starts it.

  --no-gdb   leave the stub listening and print how to attach by hand
USAGE
}

ATTACH=1
while [ $# -gt 0 ]; do
    case "$1" in
        --image) IMAGE=$2; shift 2 ;;
        --port) PORT=$2; shift 2 ;;
        --no-gdb) ATTACH=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[ -f "$QEMU" ] || { echo "no emulator at $QEMU; set QEMU=" >&2; exit 1; }
[ "$(id -u)" -ne 0 ] || { echo "invoke debugger as the non-root test account" >&2; exit 1; }
[ -n "$IMAGE" ] && [ -f "$IMAGE" ] || { echo "--image CARD is required" >&2; exit 2; }
cmp "$ROOT/tools/astra-qemu-root" "$STORAGE_HELPER" >/dev/null || {
    echo "installed astra-qemu-root is stale; reinstall tools/astra-qemu-root" >&2; exit 1;
}
[ -f "$ROM" ] || { echo "no ROM at $ROM; run make in sw/boot" >&2; exit 1; }
[ -f "$KERNEL_ELF" ] || echo "warning: no kernel symbols at $KERNEL_ELF" >&2
[ -f "$USER_ELF" ] || echo "warning: no user symbols at $USER_ELF" >&2
[ -f "$BOOT_ELF" ] || echo "warning: no ROM symbols at $BOOT_ELF" >&2

set -- sudo -n "$STORAGE_HELPER" --image "$IMAGE" -- \
    "$QEMU" -M astra68 -m "${ASTRA_MEMORY:-512M}" -bios "$ROM" -display none -monitor none \
    -serial mon:stdio -no-reboot -S -gdb "tcp::$PORT"

if [ "$ATTACH" -eq 0 ]; then
    echo "gdb: $GDB -ex 'target remote :$PORT' $KERNEL_ELF"
    exec "$@"
fi

"$@" &
QEMU_PID=$!
cleanup() {
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# The stub is listening before QEMU reports anything, but not instantly.
sleep 1

"$GDB" \
    -ex "set confirm off" \
    -ex "set architecture m68k" \
    -ex "file $KERNEL_ELF" \
    -ex "add-symbol-file $BOOT_ELF $BOOT_TEXT" \
    -ex "add-symbol-file $USER_ELF $USER_TEXT" \
    -ex "target remote :$PORT" \
    -ex "set confirm on" \
    -ex "echo \n== ROM, kernel and user symbols loaded; 'continue' to boot ==\n"
