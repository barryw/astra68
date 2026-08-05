#!/bin/sh
set -eu

ASTRA_ROOT=${ASTRA_ROOT:-/data/astra}
QEMU=${QEMU:-$ASTRA_ROOT/qemu/bin/qemu-system-m68k-astra}
ROM=${ROM:-$ASTRA_ROOT/rom/astra_boot.bin}
LIBDIR=${LIBDIR:-$ASTRA_ROOT/qemu/lib}

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
    KEYBOARD=$(find_input '*-event-kbd') || {
        echo "Astra keyboard evdev not found" >&2
        exit 1
    }
fi
if [ -z "$POINTER" ]; then
    POINTER=$(find_input '*-event-mouse') || {
        echo "Astra pointer evdev not found" >&2
        exit 1
    }
fi
if [ ! -c "$KEYBOARD" ] || [ ! -c "$POINTER" ]; then
    echo "Astra input paths must identify evdev character devices" >&2
    exit 1
fi

echo "Astra keyboard: $KEYBOARD" >&2
echo "Astra pointer:  $POINTER" >&2
exec env LD_LIBRARY_PATH="$LIBDIR" "$QEMU" \
    -M astra68 -m 32M -bios "$ROM" \
    -object input-linux,id=astra-keyboard,evdev="$KEYBOARD",repeat=off \
    -object input-linux,id=astra-pointer,evdev="$POINTER",repeat=off \
    -nographic -monitor none -serial none -no-reboot "$@"
