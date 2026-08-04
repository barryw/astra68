#!/bin/bash
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/build-usb-pll-probe"

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
    read_verilog -sv $here/../soc/ecp5pll.sv $here/../soc/uart_tx.sv $here/usb_pll_probe.sv;
    proc; opt; scc -expect 0;
    synth_ecp5 -top usb_pll_probe -json $out/usb_pll_probe.json;
    scc -expect 0;
" > "$out/yosys.log" 2>&1

nextpnr-ecp5 \
    --85k \
    --package CABGA381 \
    --json "$out/usb_pll_probe.json" \
    --lpf "$here/usb_pll_probe.lpf" \
    --textcfg "$out/usb_pll_probe.config" \
    > "$out/nextpnr.log" 2>&1

ecppack "$out/usb_pll_probe.config" "$out/usb_pll_probe.bit"
echo "built $out/usb_pll_probe.bit"
