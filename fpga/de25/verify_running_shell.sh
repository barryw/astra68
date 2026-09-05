#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
QUARTUS_ROOT=${QUARTUS_ROOT:-/home/barry/altera_pro/26.1.1/quartus}
SYSTEM_CONSOLE=${SYSTEM_CONSOLE:-$(dirname "$QUARTUS_ROOT")/syscon/bin/system-console}
SHELL_BUILD=${1:-$ROOT/build/de25/astra-shell}
SOF=$(realpath "$SHELL_BUILD/output_files/golden_top_hps.sof")

test -x "$SYSTEM_CONSOLE"
(cd "$SHELL_BUILD" && sha256sum -c BUILD_SHA256SUMS)

cd "$ROOT"
while true; do
    set +e
    ASTRA_DE25_SOF=$SOF "$SYSTEM_CONSOLE" \
        --script="$ROOT/fpga/de25/verify_running_shell.tcl"
    status=$?
    set -e
    (( status == 0 )) && break
    (( status == 2 )) || exit "$status"
done
