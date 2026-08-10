#!/bin/sh
set -eu

ASTRA_ROOT=${ASTRA_ROOT:-/data/astra}
QEMU=${QEMU:-$ASTRA_ROOT/qemu/bin/qemu-system-m68k-astra}
ROM=${ROM:-$ASTRA_ROOT/rom/astra_boot.bin}
STORAGE=${STORAGE:-$ASTRA_ROOT/storage-terminal.img}
LIBDIR=${LIBDIR:-$ASTRA_ROOT/qemu/lib}
TERMINAL_DISPLAY=${ASTRA_TERMINAL_DISPLAY:-$ASTRA_ROOT/bin/astra-terminal-display}
TEXT_PLANE=${ASTRA_TEXT_PLANE_PATH:-$ASTRA_ROOT/run/post-text.bin}
DISPLAY_MAILBOX=${ASTRA_DISPLAY_MAILBOX_PATH:-$ASTRA_ROOT/run/display.bin}
MEMORY=${ASTRA_MEMORY:-128M}

find_input()
{
    pattern=$1
    for device in /dev/input/by-id/$pattern; do
        if [ -c "$device" ]; then
            printf '%s\n' "$device"
            return 0
        fi
    done
    return 1
}

KEYBOARD=${ASTRA_KEYBOARD_EVDEV:-}
POINTER=${ASTRA_POINTER_EVDEV:-}
if [ -z "$KEYBOARD" ]; then
    KEYBOARD=$(find_input '*-event-kbd') || true
fi
if [ -z "$POINTER" ]; then
    POINTER=$(find_input '*-event-mouse') || true
fi
if [ -n "$KEYBOARD" ] && [ ! -c "$KEYBOARD" ]; then
    echo "Astra keyboard path must identify an evdev character device" >&2
    exit 1
fi
if [ -n "$POINTER" ] && [ ! -c "$POINTER" ]; then
    echo "Astra pointer path must identify an evdev character device" >&2
    exit 1
fi
if [ ! -x "$TERMINAL_DISPLAY" ]; then
    echo "Astra terminal display not found: $TERMINAL_DISPLAY" >&2
    exit 1
fi
if [ ! -r "$STORAGE" ]; then
    echo "Astra storage image not found: $STORAGE" >&2
    exit 1
fi

if [ -n "$KEYBOARD" ]; then
    echo "Astra keyboard: $KEYBOARD" >&2
    set -- -object input-linux,id=astra-keyboard,evdev="$KEYBOARD",repeat=off "$@"
else
    echo "Astra keyboard: not present" >&2
fi
if [ -n "$POINTER" ]; then
    echo "Astra pointer:  $POINTER" >&2
    set -- -object input-linux,id=astra-pointer,evdev="$POINTER",repeat=off "$@"
fi

mkdir -p "$(dirname "$TEXT_PLANE")"
dd if=/dev/zero of="$TEXT_PLANE" bs=4096 count=1 2>/dev/null
dd if=/dev/zero of="$DISPLAY_MAILBOX" bs=4096 count=451 2>/dev/null
"$TERMINAL_DISPLAY" "$TEXT_PLANE" "$DISPLAY_MAILBOX" &
display_pid=$!
cleanup()
{
    kill "$display_pid" 2>/dev/null || true
    wait "$display_pid" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

env ASTRA_TEXT_PLANE_PATH="$TEXT_PLANE" \
    ASTRA_DISPLAY_MAILBOX_PATH="$DISPLAY_MAILBOX" \
    LD_LIBRARY_PATH="$LIBDIR" "$QEMU" \
    -object memory-backend-ram,id=astra-ram,size="$MEMORY",prealloc=on \
    -M astra68,memory-backend=astra-ram -m "$MEMORY" -bios "$ROM" \
    -drive if=none,format=raw,file="$STORAGE" \
    -nographic -monitor none -serial none -no-reboot "$@"
