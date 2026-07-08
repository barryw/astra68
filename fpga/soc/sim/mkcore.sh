#!/bin/bash
# mkcore.sh -> wf68k_core.v : convert the VHDL WF68K30L core to Verilog for iverilog sim.
# setundef -zero -init gives every FF an init value of 0 (real FPGA FFs power up to 0; iverilog
# does not auto-init, so without this the netlist simulates as all-X and never leaves reset).
set -e
if [ -z "${OSS:-}" ]; then
  for d in "$HOME/oss-cad-suite" /opt/homebrew/oss-cad-suite /opt/oss-cad-suite; do
    [ -d "$d" ] && OSS="$d" && break
  done
fi
source "$OSS/environment"
export GHDL_PREFIX="$OSS/lib/ghdl"
cd "$(dirname "$0")"          # fpga/soc/sim/
C=../../cpu/wf68k30L
W=../../cpu
yosys -m ghdl -q -p "
ghdl --std=08 -fsynopsys $C/wf68k30L_pkg.vhd $C/wf68k30L_address_registers.vhd $C/wf68k30L_data_registers.vhd $C/wf68k30L_alu.vhd $C/wf68k30L_opcode_decoder.vhd $C/wf68k30L_bus_interface.vhd $C/wf68k30L_exception_handler.vhd $C/wf68k30L_control.vhd $C/wf68k30L_top.vhd $W/wf68k_wrap.vhd -e wf68k_wrap;
proc; memory_collect; setundef -zero -init;
write_verilog -noattr wf68k_core.v
"
echo "wrote wf68k_core.v ($(grep -c initial wf68k_core.v) initial blocks)"
