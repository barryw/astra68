#!/usr/bin/env bash
# WF68K30L standalone cost check on the ULX3S 85F.
#
# STATUS: BLOCKED on the open (ghdl) flow. ghdl's synth engine crashes on this
# core's mixed clocked+combinational process style (a clocked process that also
# drives combinational outputs from a variable after the clock-edge `if`):
#   --std=08 : INTERNAL_ERROR in netlists-inference.adb (control.vhd:1258, MOVEM)
#   --std=93 : "=" overload ambiguity (needs -fexplicit, then same synth crash)
# The style pervades (~81 clocked processes across 9.4k lines) — written for
# commercial synth (Lattice Diamond / Quartus / Vivado), which handle it.
#
# To get REAL numbers: synth the CPU in Lattice Diamond (free, native VHDL) and
# keep this open flow for the custom chips — or port the core's clocked processes
# to split clocked/combinational for ghdl (multi-day, correctness-sensitive).
# Kept here so `./synth.sh` reproduces the blocker.
set -euo pipefail
OSS=/opt/homebrew/oss-cad-suite/bin
export GHDL_PREFIX=/opt/homebrew/oss-cad-suite/lib/ghdl   # ghdl IEEE library location
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/wf68k30L"
B="$HERE/build"; mkdir -p "$B"

# pkg first, entities, top last (ghdl analyzes in order)
FILES="wf68k30L_pkg.vhd \
  wf68k30L_address_registers.vhd wf68k30L_data_registers.vhd wf68k30L_alu.vhd \
  wf68k30L_opcode_decoder.vhd wf68k30L_bus_interface.vhd \
  wf68k30L_exception_handler.vhd wf68k30L_control.vhd wf68k30L_top.vhd"

cd "$SRC"
echo "== yosys + ghdl synth (synth_ecp5) =="
"$OSS/yosys" -m ghdl -p "
  ghdl --std=08 -fsynopsys $FILES -e WF68K30L_TOP;
  synth_ecp5 -top WF68K30L_TOP -json $B/wf68k30l.json;
  stat -tech ecp5
"

echo "== nextpnr-ecp5 (85F utilisation + Fmax) =="
"$OSS/nextpnr-ecp5" --85k --package CABGA381 --json $B/wf68k30l.json \
  --freq 50 --report $B/rpt.json --placer-heap-cell-placement-timeout 4 2>&1 \
  | grep -iE 'Max frequency|Info: +(TRELLIS_COMB|TRELLIS_FF|TRELLIS_IO|DP16KD|MULT|ALU54|EHXPLLL|CARRY)|utilisation|ERROR:' || true
