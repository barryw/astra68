#!/bin/bash
# mkcore.sh -> astra_cpu_core.v: convert the TG68K 68030/PMMU wrapper to Verilog.
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
T030M2=../../cpu/tg68k_c_030_mmu2
W=../../cpu
CORE_OUT="${CORE_OUT:-astra_cpu_core.v}"
yosys -m ghdl -q -p "
ghdl --std=08 -fsynopsys --latches $T030M2/TG68K_Pack.vhd $T030M2/TG68K_ALU.vhd $T030M2/TG68K_PMMU_030.vhd $T030M2/TG68K_Cache_030.vhd $T030M2/TG68KdotC_Kernel.vhd $T030M2/TG68K.vhd $W/tg68k030_mmu2_wrap.vhd -e tg68k_wrap;
proc; memory_collect; setundef -zero -init;
write_verilog -noattr $CORE_OUT
"
echo "wrote $CORE_OUT ($(grep -c initial "$CORE_OUT") initial blocks)"
