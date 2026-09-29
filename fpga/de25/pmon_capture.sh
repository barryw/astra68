#!/usr/bin/env bash
# Measure the HPS-to-FPGA (Media RAM) path with the shell's PMON.
# usage: pmon_capture.sh SHELL_BUILD CONFIG BOARD_COMMAND...
# CONFIG is a PMON library config (wo, ro, basic_lat, ch_eff, ch_bp, diag).
# Example: pmon_capture.sh build/de25/astra-shell wo \
#     /data/cert/astra-arena-bandwidth 0x10000000 1331200 2
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
QUARTUS_ROOT=${QUARTUS_ROOT:-/home/barry/altera_pro/26.1.1/quartus}
SYSTEM_CONSOLE=$(dirname "$QUARTUS_ROOT")/syscon/bin/system-console
BOARD=${ASTRA_DE25_BOARD:-root@192.168.1.52}
export ASTRA_DE25_SOF=$(realpath "$1/output_files/golden_top_hps.sof")
export PMON_LIBRARY=$(dirname "$QUARTUS_ROOT")/ip/altera/pmon/util/pmon_library.tcl
config=$2
shift 2

pmon() {
    local output
    # The first System Console launch after idle can miss the JTAG device.
    for _ in 1 2 3 4; do
        output=$(PMON_CMD=$1 "$SYSTEM_CONSOLE" \
            --script="$ROOT/fpga/de25/pmon_capture.tcl" 2>&1) || true
        if grep -q "PMON Library loaded" <<<"$output"; then
            sed -n '/^>> /,$p' <<<"$output"
            return
        fi
    done
    echo "$output" >&2
    return 1
}

pmon "pmon_set $config 0 0;pmon_reset_counter_data 0 0" >/dev/null
ssh "$BOARD" "$*"
pmon "pmon_read 0 0"
