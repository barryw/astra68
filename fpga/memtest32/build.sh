#!/bin/bash
set -euo pipefail

LATENCY="${1:-3}"
case "$LATENCY" in
  1|2|3) ;;
  *) echo "usage: $0 [read-latency: 1|2|3]" >&2; exit 2 ;;
esac

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SOC="$ROOT/fpga/soc"
OUT="$HERE/build/latency${LATENCY}"

if [ -z "${OSS:-}" ]; then
  for d in "$HOME/oss-cad-suite" /opt/oss-cad-suite /usr/local/oss-cad-suite; do
    [ -d "$d" ] && OSS="$d" && break
  done
fi
[ -d "${OSS:-}" ] || { echo "oss-cad-suite not found (set OSS)" >&2; exit 1; }
source "$OSS/environment"

mkdir -p "$OUT"
yosys -p "
read_verilog -sv \
  $SOC/ecp5pll.sv \
  $SOC/uart_tx.sv \
  $SOC/thirdparty/core_sdram_axi4/sdram_axi_core.v \
  $SOC/sdram32_controller.sv \
  $HERE/astra_sdram32_hwtest.sv;
chparam -set SDRAM_READ_LATENCY $LATENCY astra_sdram32_hwtest;
proc; opt; scc -expect 0;
synth_ecp5 -top astra_sdram32_hwtest -json $OUT/astra_sdram32_hwtest.json;
scc -expect 0;
" > "$OUT/yosys.log" 2>&1

nextpnr-ecp5 --85k --package CABGA381 --freq 75 --seed 1 \
  --json "$OUT/astra_sdram32_hwtest.json" \
  --lpf "$ROOT/fpga/memtest/astra_memtest.lpf" \
  --textcfg "$OUT/astra_sdram32_hwtest.config" \
  > "$OUT/nextpnr.log" 2>&1
ecppack "$OUT/astra_sdram32_hwtest.config" "$OUT/astra_sdram32_hwtest.bit"

echo "built $OUT/astra_sdram32_hwtest.bit (SDRAM_READ_LATENCY=$LATENCY)"
grep -E 'Max frequency for clock|Device utilisation' "$OUT/nextpnr.log" || true
