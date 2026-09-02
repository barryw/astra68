#!/bin/sh
set -eu

: "${ASTRA_QEMU_BINARY:?set ASTRA_QEMU_BINARY to qemu-system-m68k}"
: "${ASTRA_PERF_DATA:?set ASTRA_PERF_DATA to the perf.data output path}"

exec perf record -F "${ASTRA_PERF_FREQUENCY:-999}" -g \
    -o "$ASTRA_PERF_DATA" -- "$ASTRA_QEMU_BINARY" -perfmap "$@"
