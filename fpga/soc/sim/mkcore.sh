#!/bin/bash
# mkcore.sh -> wf68k_core.v : convert the VHDL CPU wrappers to Verilog for sim.
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
T=../../cpu/tg68k_c
T030=../../cpu/tg68k_c_030_mmu
W=../../cpu
CPU_CORE="${CPU_CORE:-wf68k}"
CORE_OUT="${CORE_OUT:-wf68k_core.v}"
TG_GHDL=""
case "$CPU_CORE" in
  wf68k|wf68k30l|tg68k|tg68k_c)
    TG_GHDL="ghdl --std=08 -fsynopsys --latches $T/TG68K_Pack.vhd $T/TG68K_ALU.vhd $T/TG68KdotC_Kernel.vhd $W/tg68k_wrap.vhd -e tg68k_wrap;"
    ;;
  tg68k030|tg68k_mmu|tg68k_c_030_mmu)
    TG_GHDL="ghdl --std=08 -fsynopsys --latches $T030/TG68K_Pack.vhd $T030/TG68K_ALU.vhd $T030/TG68K_PMMU_030.vhd $T030/TG68K_Cache_030.vhd $T030/TG68KdotC_Kernel.vhd $T030/TG68K.vhd $W/tg68k030_wrap.vhd -e tg68k_wrap;"
    ;;
  *)
    echo "unknown CPU_CORE='$CPU_CORE' (expected wf68k, tg68k, or tg68k030)" >&2
    exit 2
    ;;
esac
yosys -m ghdl -q -p "
ghdl --std=08 -fsynopsys $C/wf68k30L_pkg.vhd $C/wf68k30L_address_registers.vhd $C/wf68k30L_data_registers.vhd $C/wf68k30L_alu.vhd $C/wf68k30L_opcode_decoder.vhd $C/wf68k30L_bus_interface.vhd $C/wf68k30L_exception_handler.vhd $C/wf68k30L_control.vhd $C/wf68k30L_top.vhd $W/wf68k_wrap.vhd -e wf68k_wrap;
$TG_GHDL
proc; memory_collect; setundef -zero -init;
write_verilog -noattr $CORE_OUT
"
echo "wrote $CORE_OUT ($(grep -c initial "$CORE_OUT") initial blocks)"
