#!/bin/bash
# mkbit.sh <rom.hex> [tag]  -> astra.bit
# Builds the canonical repo SoC (fpga/soc/*.sv + fpga/cpu/wf68k30L) via yosys+nextpnr.
# nextpnr rc=0 == combinational-loop-free (it refuses loops instead of cutting them).
set -e
OSS=/opt/homebrew/oss-cad-suite
source "$OSS/environment"
export GHDL_PREFIX="$OSS/lib/ghdl"
cd "$(dirname "$0")"                 # fpga/soc/oss_flow/
SOC=..                              # fpga/soc/
C=../../cpu/wf68k30L               # fpga/cpu/wf68k30L/
W=../../cpu                        # fpga/cpu/ (wf68k_wrap.vhd)
TAG="${2:-build}"
cp "$1" rom_init.hex                 # ROM for this build (astra_soc.sv $readmemh is cwd-relative)
CORES="$C/wf68k30L_pkg.vhd $C/wf68k30L_address_registers.vhd $C/wf68k30L_data_registers.vhd \
$C/wf68k30L_alu.vhd $C/wf68k30L_opcode_decoder.vhd $C/wf68k30L_bus_interface.vhd \
$C/wf68k30L_exception_handler.vhd $C/wf68k30L_control.vhd $C/wf68k30L_top.vhd $W/wf68k_wrap.vhd"
yosys -m ghdl -q -p "
ghdl --std=08 -fsynopsys $CORES -e wf68k_wrap;
read_verilog -sv $SOC/uart_tx.sv $SOC/uart_rx.sv $SOC/astra_soc.sv;
synth_ecp5 -top astra_soc -json astra.json;
" > "yosys_${TAG}.log" 2>&1
nextpnr-ecp5 --85k --package CABGA381 --json astra.json --lpf "$SOC/astra_soc.lpf" --textcfg astra.config > "pnr_${TAG}.log" 2>&1
echo "nextpnr rc=$? (0=routed, loop-free)"
ecppack astra.config astra.bit
echo "built astra.bit from $1"
