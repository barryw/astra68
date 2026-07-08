#!/bin/bash
# Astra68 comb-loop detector. yosys `check` names loops (readable signals);
# nextpnr is the ground-truth oracle that hard-refuses them.
OSS=/opt/homebrew/oss-cad-suite
source $OSS/environment
export GHDL_PREFIX=$OSS/lib/ghdl
cd "$(dirname "$0")"
C=/Users/barry/Git/astra68/fpga/cpu/wf68k30L
W=/Users/barry/Git/astra68/fpga/cpu
CORES="$C/wf68k30L_pkg.vhd $C/wf68k30L_address_registers.vhd $C/wf68k30L_data_registers.vhd \
$C/wf68k30L_alu.vhd $C/wf68k30L_opcode_decoder.vhd $C/wf68k30L_bus_interface.vhd \
$C/wf68k30L_exception_handler.vhd $C/wf68k30L_control.vhd $C/wf68k30L_top.vhd $W/wf68k_wrap.vhd"

yosys -m ghdl -q -p "
ghdl --std=08 -fsynopsys $CORES -e wf68k_wrap;
read_verilog -sv uart_tx.sv astra_soc.sv;
hierarchy -top astra_soc;
proc;
check;
synth_ecp5 -top astra_soc -json astra.json;
" 2>&1 | tee yosys.log | grep -iE "logic loop|found.*loop|warning.*loop|\\\$_|error" | head -40
echo "=== yosys logic-loop lines ==="
grep -iE "logic loop|wire.*loop" yosys.log | head -60
echo "=== nextpnr (loop oracle) ==="
nextpnr-ecp5 --85k --package CABGA381 --json astra.json --lpf astra_soc.lpf --textcfg astra.config > pnr.log 2>&1
echo "nextpnr rc=$?"
grep -iE "comb.*loop|logic loop|cycle|ERROR" pnr.log | head -40
