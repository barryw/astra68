#!/bin/bash
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/build"

if [ -z "${OSS:-}" ]; then
    for candidate in \
        "$HOME/oss-cad-suite" \
        "$HOME/oss-cad-suite-install/oss-cad-suite" \
        /opt/oss-cad-suite \
        /usr/local/oss-cad-suite; do
        if [ -d "$candidate" ]; then
            OSS="$candidate"
            break
        fi
    done
fi
[ -d "${OSS:-}" ] || {
    echo "oss-cad-suite not found (set OSS)" >&2
    exit 1
}

# shellcheck disable=SC1091
source "$OSS/environment"
mkdir -p "$out"

yosys -q -p "
    read_verilog -sv $here/esp32_passthru.sv;
    proc; opt; scc -expect 0;
    synth_ecp5 -top astra_esp32_passthru -json $out/esp32_passthru.json;
    scc -expect 0;
" > "$out/yosys.log" 2>&1

nextpnr-ecp5 \
    --85k \
    --package CABGA381 \
    --json "$out/esp32_passthru.json" \
    --lpf "$here/esp32_passthru.lpf" \
    --textcfg "$out/esp32_passthru.config" \
    > "$out/nextpnr.log" 2>&1

ecppack "$out/esp32_passthru.config" "$out/esp32_passthru.bit"
echo "built $out/esp32_passthru.bit"
