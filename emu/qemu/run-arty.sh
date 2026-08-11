#!/bin/sh
set -eu

ASTRA_ROOT=${ASTRA_ROOT:-/data/astra}
QEMU=${QEMU:-$ASTRA_ROOT/qemu/bin/qemu-system-m68k-astra}
ROM=${ROM:-$ASTRA_ROOT/rom/astra_boot.bin}
STORAGE=${STORAGE:-$ASTRA_ROOT/storage-terminal.img}
LIBDIR=${LIBDIR:-$ASTRA_ROOT/qemu/lib}
TERMINAL_DISPLAY=${ASTRA_TERMINAL_DISPLAY:-$ASTRA_ROOT/bin/astra-terminal-display}
INPUT_HOTPLUG=${ASTRA_INPUT_HOTPLUG:-$ASTRA_ROOT/bin/astra-input-hotplug.py}
TEXT_PLANE=${ASTRA_TEXT_PLANE_PATH:-$ASTRA_ROOT/run/post-text.bin}
DISPLAY_MAILBOX=${ASTRA_DISPLAY_MAILBOX_PATH:-$ASTRA_ROOT/run/display.bin}
QMP_SOCKET=${ASTRA_QMP_SOCKET:-$ASTRA_ROOT/run/qmp.sock}
MEMORY=${ASTRA_MEMORY:-128M}
if [ ! -x "$TERMINAL_DISPLAY" ]; then
    echo "Astra terminal display not found: $TERMINAL_DISPLAY" >&2
    exit 1
fi
if [ ! -r "$STORAGE" ]; then
    echo "Astra storage image not found: $STORAGE" >&2
    exit 1
fi
if [ ! -r "$INPUT_HOTPLUG" ]; then
    echo "Astra input hotplug service not found: $INPUT_HOTPLUG" >&2
    exit 1
fi

mkdir -p "$(dirname "$TEXT_PLANE")" "$(dirname "$QMP_SOCKET")"
exec 9>"$(dirname "$QMP_SOCKET")/runtime.lock"
if ! flock -n 9; then
    echo "Astra runtime is already active" >&2
    exit 1
fi
rm -f "$QMP_SOCKET"
dd if=/dev/zero of="$TEXT_PLANE" bs=4096 count=1 2>/dev/null
dd if=/dev/zero of="$DISPLAY_MAILBOX" bs=4096 count=451 2>/dev/null
"$TERMINAL_DISPLAY" "$TEXT_PLANE" "$DISPLAY_MAILBOX" &
display_pid=$!
input_pid=
qemu_pid=
stop_process()
{
    pid=$1
    if [ -z "$pid" ]; then
        return
    fi
    kill "$pid" 2>/dev/null || true
    count=0
    while kill -0 "$pid" 2>/dev/null && [ "$count" -lt 10 ]; do
        state=$(sed -n 's/^State:[[:space:]]*\([^[:space:]]\).*/\1/p' \
            "/proc/$pid/status" 2>/dev/null || true)
        [ "$state" = "Z" ] && break
        count=$((count + 1))
        sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
        state=$(sed -n 's/^State:[[:space:]]*\([^[:space:]]\).*/\1/p' \
            "/proc/$pid/status" 2>/dev/null || true)
        [ "$state" = "Z" ] || kill -KILL "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
}
cleanup()
{
    stop_process "$input_pid"
    input_pid=
    stop_process "$qemu_pid"
    qemu_pid=
    stop_process "$display_pid"
    display_pid=
    rm -f "$QMP_SOCKET"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

env ASTRA_TEXT_PLANE_PATH="$TEXT_PLANE" \
    ASTRA_DISPLAY_MAILBOX_PATH="$DISPLAY_MAILBOX" \
    LD_LIBRARY_PATH="$LIBDIR" "$QEMU" \
    -object memory-backend-ram,id=astra-ram,size="$MEMORY",prealloc=on \
    -M astra68,memory-backend=astra-ram -m "$MEMORY" -bios "$ROM" \
    -drive if=none,format=raw,file="$STORAGE" \
    -nographic -monitor none -serial none -no-reboot \
    -qmp "unix:$QMP_SOCKET,server=on,wait=off" "$@" &
qemu_pid=$!
python3 "$INPUT_HOTPLUG" --qmp "$QMP_SOCKET" &
input_pid=$!
set +e
wait "$qemu_pid"
status=$?
set -e
qemu_pid=
exit "$status"
