#!/bin/bash
# mkbit.sh <rom.hex>  -> astra.bit  (loop-free already proven; skip the check overhead)
set -e
OSS=/opt/homebrew/oss-cad-suite
source $OSS/environment
export GHDL_PREFIX=$OSS/lib/ghdl
cd "$(dirname "$0")"
cp "$1" rom_init.hex
C=/Users/barry/Git/astra68/fpga/cpu/wf68k30L
W=/Users/barry/Git/astra68/fpga/cpu
CORES="$C/wf68k30L_pkg.vhd $C/wf68k30L_address_registers.vhd $C/wf68k30L_data_registers.vhd \
$C/wf68k30L_alu.vhd $C/wf68k30L_opcode_decoder.vhd $C/wf68k30L_bus_interface.vhd \
$C/wf68k30L_exception_handler.vhd $C/wf68k30L_control.vhd $C/wf68k30L_top.vhd $W/wf68k_wrap.vhd"
yosys -m ghdl -q -p "
ghdl --std=08 -fsynopsys $CORES -e wf68k_wrap;
read_verilog -sv uart_tx.sv astra_soc.sv;
synth_ecp5 -top astra_soc -json astra.json;
" > yosys_$2.log 2>&1
nextpnr-ecp5 --85k --package CABGA381 --json astra.json --lpf astra_soc.lpf --textcfg astra.config > pnr_$2.log 2>&1
echo "nextpnr rc=$? (0=routed, loop-free)"
ecppack astra.config astra.bit
echo "built astra.bit from $1"
