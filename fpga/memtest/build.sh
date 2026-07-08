#!/usr/bin/env bash
# Astra 68 SDRAM memtest — build + flash for ULX3S 85F.
#   ./build.sh          build bitstream
#   ./build.sh flash    build + load to SRAM (temporary, reverts on power-cycle)
set -euo pipefail

OSS=/opt/homebrew/oss-cad-suite/bin
E6502="$HOME/Git/e6502/e6502.FPGA/rtl"
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HERE/build"
mkdir -p "$B"

DEVICE=--85k
PKG=CABGA381

echo "== yosys synth =="
"$OSS/yosys" -q -p "
  read_verilog -sv -DSYNTHESIS $HERE/astra_memtest.sv $E6502/ecp5pll.sv $E6502/uart_tx.sv;
  read_verilog -DSYNTHESIS $E6502/sdram/sdram.v;
  synth_ecp5 -top astra_memtest -json $B/astra_memtest.json
"

echo "== nextpnr =="
"$OSS/nextpnr-ecp5" $DEVICE --package $PKG --freq 75 \
  --json $B/astra_memtest.json --lpf "$HERE/astra_memtest.lpf" \
  --textcfg $B/astra_memtest.config

echo "== ecppack =="
"$OSS/ecppack" --compress $B/astra_memtest.config $B/astra_memtest.bit
echo "bitstream: $B/astra_memtest.bit"

if [ "${1:-}" = "flash" ]; then
  echo "== load to SRAM (temporary) =="
  openFPGALoader --board ulx3s "$B/astra_memtest.bit"
fi
