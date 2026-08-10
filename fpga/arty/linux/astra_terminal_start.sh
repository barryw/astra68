#!/bin/sh
set -eu

ASTRA_ROOT=${ASTRA_ROOT:-/data/astra}

while ! mountpoint -q /data 2>/dev/null; do
    sleep 1
done

while :; do
    for keyboard in /dev/input/by-id/*-event-kbd; do
        if [ -c "$keyboard" ]; then
            exec "$ASTRA_ROOT/bin/run-arty.sh"
        fi
    done
    sleep 1
done
