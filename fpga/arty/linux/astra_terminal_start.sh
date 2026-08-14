#!/bin/sh
set -eu

ASTRA_ROOT=${ASTRA_ROOT:-/data/astra}
DATA_PATH=${ASTRA_DATA_PATH:-/data}
RUN_ARTY=${ASTRA_RUN_ARTY:-$ASTRA_ROOT/bin/run-arty.sh}
RESTART_LIMIT=${ASTRA_RESTART_LIMIT:-3}
RESTART_DELAY=${ASTRA_RESTART_DELAY:-1}
ASTRA_FRONT_PANEL_MMIO_PATH=${ASTRA_FRONT_PANEL_MMIO_PATH:-/dev/mem}
ASTRA_FRONT_PANEL_MMIO_OFFSET=${ASTRA_FRONT_PANEL_MMIO_OFFSET:-0x43c07000}
export ASTRA_FRONT_PANEL_MMIO_PATH ASTRA_FRONT_PANEL_MMIO_OFFSET

while ! mountpoint -q "$DATA_PATH" 2>/dev/null; do
    sleep 1
done

attempt=0
while :; do
    set +e
    "$RUN_ARTY"
    status=$?
    set -e
    case "$status" in
        132|134|135|136|139) ;;
        *) exit "$status" ;;
    esac
    attempt=$((attempt + 1))
    if [ "$attempt" -gt "$RESTART_LIMIT" ]; then
        echo "Astra runtime crash limit reached (status $status)" >&2
        exit "$status"
    fi
    echo "Astra runtime crashed (status $status); restart $attempt/$RESTART_LIMIT" >&2
    sleep "$RESTART_DELAY"
done
