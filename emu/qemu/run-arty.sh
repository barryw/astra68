#!/bin/sh
set -eu

ASTRA_STORE=${ASTRA_STORE:-/var/lib/astra}
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
FONT_WORKER=$ASTRA_ROOT/bin/astra-font-worker
if [ ! -x "$FONT_WORKER" ]; then
    echo "Astra font worker is missing: $FONT_WORKER" >&2; exit 1
fi
ROM=${ROM:-$ASTRA_ROOT/rom/astra_boot.bin}
BASE_STORAGE=${ASTRA_BASE_STORAGE:-$ASTRA_ROOT/storage-terminal.img}
LIBDIR=${LIBDIR:-$ASTRA_ROOT/qemu/lib}
TERMINAL_DISPLAY=${ASTRA_TERMINAL_DISPLAY:-$ASTRA_ROOT/bin/astra-terminal-display}
INPUT_HOTPLUG=${ASTRA_INPUT_HOTPLUG:-$ASTRA_ROOT/bin/astra-input-hotplug.py}
STATE_ROOT=${ASTRA_STATE_ROOT:-$ASTRA_STORE/state/system}
RUN_ROOT=${ASTRA_RUN_ROOT:-/run/astra}
LOG_ROOT=${ASTRA_LOG_ROOT:-$ASTRA_STORE/log}
TEXT_PLANE=${ASTRA_TEXT_PLANE_PATH:-$RUN_ROOT/post-text.bin}
DISPLAY_MAILBOX=${ASTRA_DISPLAY_MAILBOX_PATH:-$RUN_ROOT/display.bin}
# The payload is the DE25 host arena when its driver is loaded: the render
# engine reads batches from it in place. Elsewhere it is an ordinary file.
HOST_ARENA=/dev/astra-host-arena
if [ -n "${ASTRA_DISPLAY_PAYLOAD_PATH:-}" ]; then
    DISPLAY_PAYLOAD=$ASTRA_DISPLAY_PAYLOAD_PATH
elif [ -c "$HOST_ARENA" ]; then
    DISPLAY_PAYLOAD=$HOST_ARENA
else
    DISPLAY_PAYLOAD=$RUN_ROOT/display-payload.bin
fi
EXPORTS_ROOT=${ASTRA_EXPORTS_ROOT:-$ASTRA_STORE/hostfs}
STORAGE_PROVIDER=$ASTRA_ROOT/bin/astra-storage
IMAGE_SELECTOR=$ASTRA_ROOT/bin/storage_image.py
if [ -n "${ASTRA_HOSTFS_ROOT+x}" ]; then
    echo "ASTRA_HOSTFS_ROOT retired; configure ASTRA_EXPORTS_ROOT" >&2
    exit 1
fi
# DE25: 8 KiB per description/object plus 40 KiB per DIR stays below 64 MiB.
HOSTFS_MAX_DESCRIPTORS=${ASTRA_HOSTFS_MAX_DESCRIPTORS-2048}
HOSTFS_MAX_DIRECTORIES=${ASTRA_HOSTFS_MAX_DIRECTORIES-1024}
QMP_SOCKET=${ASTRA_QMP_SOCKET:-$RUN_ROOT/qemu/qmp.sock}
REMOTE_DESKTOP_QMP_SOCKET=${ASTRA_REMOTE_DESKTOP_QMP_SOCKET:-$RUN_ROOT/qemu/remote-desktop-qmp.sock}
CONSOLE_LOG=${ASTRA_CONSOLE_LOG:-$LOG_ROOT/qemu-console.log}
PANIC_LOG=${ASTRA_PANIC_LOG:-$LOG_ROOT/panic-latest.log}
MEMORY=${ASTRA_MEMORY:-512M}
AUX_CPU=${ASTRA_AUX_CPU:-0}
DISPLAY_CPU=${ASTRA_DISPLAY_CPU:-1}
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
if ! DISPLAY_MAILBOX_BYTES=$(
        "$TERMINAL_DISPLAY" --mailbox-bytes); then
    echo "Cannot determine Astra display mailbox size" >&2
    exit 1
fi
case "$DISPLAY_MAILBOX_BYTES" in
''|*[!0-9]*|0) echo "Astra display mailbox size is invalid" >&2; exit 1 ;;
esac
if ! DISPLAY_PAYLOAD_BYTES=$(
        "$TERMINAL_DISPLAY" --payload-bytes); then
    echo "Cannot determine Astra display payload size" >&2
    exit 1
fi
case "$DISPLAY_PAYLOAD_BYTES" in
''|*[!0-9]*|0) echo "Astra display payload size is invalid" >&2; exit 1 ;;
esac
if ! HOST_PAGE_BYTES=$(getconf PAGESIZE); then
    echo "Cannot determine host page size" >&2
    exit 1
fi
case "$HOST_PAGE_BYTES" in
''|*[!0-9]*|0) echo "Host page size is invalid" >&2; exit 1 ;;
esac
DISPLAY_MAILBOX_STORAGE_BYTES=$((
    (DISPLAY_MAILBOX_BYTES + HOST_PAGE_BYTES - 1) /
    HOST_PAGE_BYTES * HOST_PAGE_BYTES))
if [ ! -r "$BASE_STORAGE" ]; then
    echo "Astra base storage image not found: $BASE_STORAGE" >&2
    exit 1
fi
if [ ! -r "$INPUT_HOTPLUG" ]; then
    echo "Astra input hotplug service not found: $INPUT_HOTPLUG" >&2
    exit 1
fi
case "$AUX_CPU" in
    ''|*[!0-9]*) echo "Astra helper CPU assignments are invalid" >&2; exit 1 ;;
esac
case "$DISPLAY_CPU" in
    ''|*[!0-9]*) echo "Astra helper CPU assignments are invalid" >&2; exit 1 ;;
esac
if [ "$AUX_CPU" = "$DISPLAY_CPU" ]; then
    echo "Astra auxiliary and display CPUs must be distinct" >&2
    exit 1
fi
if ! command -v taskset >/dev/null 2>&1; then
    echo "Astra helper CPU assignment requires taskset" >&2
    exit 1
fi

[ "$(id -u)" -eq 0 ] || {
    echo "Astra runtime requires its root storage service launcher" >&2; exit 1;
}
QEMU_UID=$(id -u "${ASTRA_QEMU_USER:-astra}")
QEMU_GID=$(id -g "${ASTRA_QEMU_USER:-astra}")
if [ "$QEMU_UID" -eq 0 ] || [ "$QEMU_GID" -eq 0 ]; then
    echo "Astra QEMU requires an unprivileged user and group" >&2; exit 1
fi
[ -f "$STORAGE_PROVIDER" ] && [ -x "$STORAGE_PROVIDER" ] || {
    echo "Astra storage component missing: $STORAGE_PROVIDER" >&2; exit 1;
}
[ -f "$IMAGE_SELECTOR" ] && [ -r "$IMAGE_SELECTOR" ] || {
    echo "Astra storage component missing: $IMAGE_SELECTOR" >&2; exit 1;
}
for export in "$EXPORTS_ROOT/work" "$EXPORTS_ROOT/sound"; do
    [ -d "$export" ] && [ ! -L "$export" ] || {
        echo "Astra export must be provisioned: $export" >&2; exit 1;
    }
done
mkdir -p "$(dirname "$TEXT_PLANE")" "$(dirname "$QMP_SOCKET")" \
    "$(dirname "$REMOTE_DESKTOP_QMP_SOCKET")" \
    "$(dirname "$PANIC_LOG")" "$STATE_ROOT"
exec 9>"$RUN_ROOT/runtime.lock"
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
if ! storage_selection=$(PYTHONDONTWRITEBYTECODE=1 python3 "$IMAGE_SELECTOR" "$STORAGE"); then
    echo "Astra runtime image selection failed" >&2; exit 1
fi
read -r storage_offset storage_length storage_uuid storage_extra <<EOF
$storage_selection
EOF
[ -z "$storage_extra" ] && [ -n "$storage_uuid" ] || {
    echo "Astra runtime image selection malformed" >&2; exit 1;
}
sound_seed=none
[ ! -d "$ASTRA_ROOT/soundfonts" ] || sound_seed=$ASTRA_ROOT/soundfonts
rm -f "$QMP_SOCKET" "$REMOTE_DESKTOP_QMP_SOCKET"
dd if=/dev/zero of="$TEXT_PLANE" bs=4096 count=1 2>/dev/null
dd if=/dev/zero of="$DISPLAY_MAILBOX" bs="$DISPLAY_MAILBOX_STORAGE_BYTES" \
    count=1 2>/dev/null
if [ ! -c "$DISPLAY_PAYLOAD" ]; then
    dd if=/dev/zero of="$DISPLAY_PAYLOAD" bs="$DISPLAY_PAYLOAD_BYTES" \
        count=1 2>/dev/null
fi
if [ "$(id -u)" -eq 0 ]; then
    # Root owns the buffer names; QEMU can change their bytes, not replace them.
    chmod 0700 "$STATE_ROOT"
    chmod 0600 "$STORAGE"
    chgrp "$QEMU_GID" "$TEXT_PLANE" "$DISPLAY_MAILBOX"
    chmod 0660 "$TEXT_PLANE" "$DISPLAY_MAILBOX"
    if [ ! -c "$DISPLAY_PAYLOAD" ]; then
        chgrp "$QEMU_GID" "$DISPLAY_PAYLOAD"
        chmod 0660 "$DISPLAY_PAYLOAD"
    fi
    install -d -o "$QEMU_UID" -g "$QEMU_GID" -m 0700 \
        "$(dirname "$QMP_SOCKET")" "$(dirname "$REMOTE_DESKTOP_QMP_SOCKET")"
fi
exec env ASTRA_TEXT_PLANE_PATH="$TEXT_PLANE" \
    ASTRA_STORAGE_NAME_STATE="$STATE_ROOT" \
    ASTRA_FONT_WORKER_PATH="$FONT_WORKER" \
    ASTRA_DISPLAY_MAILBOX_PATH="$DISPLAY_MAILBOX" \
    ASTRA_DISPLAY_PAYLOAD_PATH="$DISPLAY_PAYLOAD" \
    ASTRA_HOSTFS_MAX_DESCRIPTORS="$HOSTFS_MAX_DESCRIPTORS" \
    ASTRA_HOSTFS_MAX_DIRECTORIES="$HOSTFS_MAX_DIRECTORIES" \
    LD_LIBRARY_PATH="$LIBDIR" python3 "$ASTRA_ROOT/bin/runtime-supervisor.py" \
    --display "$TERMINAL_DISPLAY" "$TEXT_PLANE" "$DISPLAY_MAILBOX" "$DISPLAY_PAYLOAD" \
    --display-cpu "$DISPLAY_CPU" --input "$INPUT_HOTPLUG" --aux-cpu "$AUX_CPU" \
    --qmp "$QMP_SOCKET" --remote-qmp "$REMOTE_DESKTOP_QMP_SOCKET" \
    --uid "$QEMU_UID" --gid "$QEMU_GID" \
    --console "$CONSOLE_LOG" --panic "$PANIC_LOG" -- \
    "$STORAGE_PROVIDER" --uid "$QEMU_UID" --gid "$QEMU_GID" \
    --system "$STORAGE" "$storage_offset" "$storage_length" "$storage_uuid" \
    --work "$EXPORTS_ROOT/work" --sound "$EXPORTS_ROOT/sound" \
    --limits 8388608 8388608 4 10000 4 100663296 8192 67108864 4096 33554432 4096 \
    --discovery udev --seed-sound "$sound_seed" -- "$QEMU" \
    -object memory-backend-ram,id=astra-ram,size="$MEMORY",prealloc=on \
    -M astra68,memory-backend=astra-ram -m "$MEMORY" -bios "$ROM" \
    -display none -monitor none -serial stdio \
    -qmp "unix:$QMP_SOCKET,server=on,wait=off" \
    -qmp "unix:$REMOTE_DESKTOP_QMP_SOCKET,server=on,wait=off" "$@"
