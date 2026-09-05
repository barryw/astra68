#!/bin/sh
set -eu

ASTRA_STORE=${ASTRA_STORE:-/data/astra}
ASTRA_ROOT=${ASTRA_ROOT:-$ASTRA_STORE/current}
if ! ASTRA_ROOT=$(readlink -f "$ASTRA_ROOT"); then
    echo "Astra release is not selected: $ASTRA_ROOT" >&2
    exit 1
fi
RELEASE_TOOL=${ASTRA_RELEASE_TOOL:-$ASTRA_ROOT/bin/astra-release.py}
if [ ! -r "$RELEASE_TOOL" ]; then
    echo "Astra release verifier not found: $RELEASE_TOOL" >&2
    exit 1
fi
if ! RELEASE_ID=$(PYTHONDONTWRITEBYTECODE=1 \
        python3 "$RELEASE_TOOL" verify --installed "$ASTRA_ROOT"); then
    echo "Astra release verification failed: $ASTRA_ROOT" >&2
    exit 1
fi
case "$RELEASE_ID" in
    *[!0-9a-f]*)
        echo "Astra release identity is invalid: $RELEASE_ID" >&2
        exit 1 ;;
esac
if [ "${#RELEASE_ID}" -ne 64 ]; then
    echo "Astra release identity is invalid: $RELEASE_ID" >&2
    exit 1
fi
QEMU=${QEMU:-$ASTRA_ROOT/qemu/bin/qemu-system-m68k-astra}
ROM=${ROM:-$ASTRA_ROOT/rom/astra_boot.bin}
BASE_STORAGE=${ASTRA_BASE_STORAGE:-$ASTRA_ROOT/storage-terminal.img}
LIBDIR=${LIBDIR:-$ASTRA_ROOT/qemu/lib}
TERMINAL_DISPLAY=${ASTRA_TERMINAL_DISPLAY:-$ASTRA_ROOT/bin/astra-terminal-display}
INPUT_HOTPLUG=${ASTRA_INPUT_HOTPLUG:-$ASTRA_ROOT/bin/astra-input-hotplug.py}
STATE_ROOT=${ASTRA_STATE_ROOT:-$ASTRA_STORE/state/$RELEASE_ID}
RUN_ROOT=${ASTRA_RUN_ROOT:-/run/astra}
LOG_ROOT=${ASTRA_LOG_ROOT:-$ASTRA_STORE/log}
TEXT_PLANE=${ASTRA_TEXT_PLANE_PATH:-$RUN_ROOT/post-text.bin}
DISPLAY_MAILBOX=${ASTRA_DISPLAY_MAILBOX_PATH:-$RUN_ROOT/display.bin}
HOSTFS_ROOT=${ASTRA_HOSTFS_ROOT:-$ASTRA_STORE/hostfs}
QMP_SOCKET=${ASTRA_QMP_SOCKET:-$RUN_ROOT/qmp.sock}
CONSOLE_LOG=${ASTRA_CONSOLE_LOG:-$LOG_ROOT/qemu-console.log}
PANIC_LOG=${ASTRA_PANIC_LOG:-$LOG_ROOT/panic-latest.log}
MEMORY=${ASTRA_MEMORY:-128M}
HOST_TIME_MIN=${ASTRA_HOST_TIME_MIN:-1735689600}
case "$HOST_TIME_MIN" in
    ''|*[!0-9]*) echo "Astra host clock check is invalid" >&2; exit 1 ;;
esac
while :; do
    host_epoch=$(date -u +%s)
    case "$host_epoch" in
        ''|*[!0-9]*) echo "Astra host clock check is invalid" >&2; exit 1 ;;
    esac
    [ "$host_epoch" -ge "$HOST_TIME_MIN" ] && break
    sleep 1
done
if [ ! -x "$TERMINAL_DISPLAY" ]; then
    echo "Astra terminal display not found: $TERMINAL_DISPLAY" >&2
    exit 1
fi
if [ ! -r "$BASE_STORAGE" ]; then
    echo "Astra base storage image not found: $BASE_STORAGE" >&2
    exit 1
fi
if [ ! -r "$INPUT_HOTPLUG" ]; then
    echo "Astra input hotplug service not found: $INPUT_HOTPLUG" >&2
    exit 1
fi

mkdir -p "$(dirname "$TEXT_PLANE")" "$(dirname "$QMP_SOCKET")" \
    "$(dirname "$PANIC_LOG")" "$HOSTFS_ROOT" "$STATE_ROOT"
exec 9>"$(dirname "$QMP_SOCKET")/runtime.lock"
if ! flock -n 9; then
    echo "Astra runtime is already active" >&2
    exit 1
fi
if [ -z "${STORAGE+x}" ]; then
    STORAGE=$STATE_ROOT/storage-terminal.img
    if [ ! -e "$STORAGE" ]; then
        storage_temporary=$STATE_ROOT/.storage-terminal.img.$$.new
        trap 'rm -f "$storage_temporary"' EXIT
        cp "$BASE_STORAGE" "$storage_temporary"
        chmod 0600 "$storage_temporary"
        sync
        mv "$storage_temporary" "$STORAGE"
        storage_temporary=
        trap - EXIT HUP INT TERM
    fi
fi
if [ ! -f "$STORAGE" ] || [ -L "$STORAGE" ] || [ ! -w "$STORAGE" ]; then
    echo "Astra runtime storage is not a writable regular file: $STORAGE" >&2
    exit 1
fi
rm -f "$QMP_SOCKET"
dd if=/dev/zero of="$TEXT_PLANE" bs=4096 count=1 2>/dev/null
dd if=/dev/zero of="$DISPLAY_MAILBOX" bs=4096 count=451 2>/dev/null
"$TERMINAL_DISPLAY" "$TEXT_PLANE" "$DISPLAY_MAILBOX" &
display_pid=$!
input_pid=
qemu_pid=
log_pid=
console_pipe="$(dirname "$QMP_SOCKET")/qemu-console.pipe"
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
    stop_process "$log_pid"
    log_pid=
    stop_process "$display_pid"
    display_pid=
    rm -f "$QMP_SOCKET" "$console_pipe"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

rm -f "$console_pipe"
mkfifo "$console_pipe"
tee "$CONSOLE_LOG" <"$console_pipe" &
log_pid=$!
env ASTRA_TEXT_PLANE_PATH="$TEXT_PLANE" \
    ASTRA_DISPLAY_MAILBOX_PATH="$DISPLAY_MAILBOX" \
    ASTRA_HOSTFS_ROOT="$HOSTFS_ROOT" \
    LD_LIBRARY_PATH="$LIBDIR" "$QEMU" \
    -object memory-backend-ram,id=astra-ram,size="$MEMORY",prealloc=on \
    -M astra68,memory-backend=astra-ram -m "$MEMORY" -bios "$ROM" \
    -drive if=none,format=raw,file="$STORAGE" \
    -display none -monitor none -serial stdio \
    -qmp "unix:$QMP_SOCKET,server=on,wait=off" "$@" \
    >"$console_pipe" 2>&1 &
qemu_pid=$!
python3 "$INPUT_HOTPLUG" --qmp "$QMP_SOCKET" &
input_pid=$!
set +e
wait "$qemu_pid"
status=$?
set -e
qemu_pid=
wait "$log_pid" 2>/dev/null || true
log_pid=
if grep -q "\*\*\* AXIOM KERNEL PANIC \*\*\*" "$CONSOLE_LOG"; then
    cp "$CONSOLE_LOG" "$PANIC_LOG"
    sleep 0.1
fi
trap - EXIT
cleanup
exit "$status"
