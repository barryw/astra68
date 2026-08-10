#!/bin/sh
set -eu

ASTRA_ROOT=${ASTRA_ROOT:-/data/astra}

while ! mountpoint -q /data 2>/dev/null; do
    sleep 1
done

exec "$ASTRA_ROOT/bin/run-arty.sh"
