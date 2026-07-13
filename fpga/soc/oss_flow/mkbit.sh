#!/bin/bash
# mkbit.sh <rom.hex> [tag] [cpu_core]  -> astra.bit
# Builds the canonical repo SoC via yosys+nextpnr. cpu_core defaults to wf68k;
# set CPU_CORE=tg68k or pass tg68k as the third argument for the candidate core.
# nextpnr rc=0 == routed. Check yosys_<tag>.log separately for loop warnings.
set -e
# Locate oss-cad-suite: $OSS override, then common install dirs (Linux NUC, macOS brew).
if [ -z "${OSS:-}" ]; then
  for d in "$HOME/oss-cad-suite" /opt/homebrew/oss-cad-suite /opt/oss-cad-suite /usr/local/oss-cad-suite; do
    [ -d "$d" ] && OSS="$d" && break
  done
fi
[ -d "${OSS:-}" ] || { echo "oss-cad-suite not found (set \$OSS)"; exit 1; }
source "$OSS/environment"
export GHDL_PREFIX="$OSS/lib/ghdl"
cd "$(dirname "$0")"                 # fpga/soc/oss_flow/
SOC=..                              # fpga/soc/
H=$SOC/hdmi                         # NovaVM-proven hdl-util HDMI pipeline
C=../../cpu/wf68k30L               # fpga/cpu/wf68k30L/
T=../../cpu/tg68k_c                # fpga/cpu/tg68k_c/
T030=../../cpu/tg68k_c_030_mmu    # fpga/cpu/tg68k_c_030_mmu/
W=../../cpu                        # fpga/cpu/ wrappers
TAG="${2:-build}"
CPU_CORE="${CPU_CORE:-${3:-wf68k}}"
SYNTH_ECP5_FLAGS="${SYNTH_ECP5_FLAGS:-}"
TARGET_FREQ_MHZ="${TARGET_FREQ_MHZ:-12}"
PNR_SEED="${PNR_SEED:-1}"
YOSYS_CPU_PARAM=""
YOSYS_MONITOR_PARAM=""
YOSYS_SDRAM_PARAM=""
YOSYS_HDMI_PARAM=""
YOSYS_CLOCK_PARAM=""
YOSYS_BUILD_PARAM=""
PNR_HDMI_FLAGS=""
TG_GHDL=""
case "$CPU_CORE" in
  wf68k|wf68k30l)
    CPU_CORE="wf68k"
    YOSYS_CPU_PARAM="chparam -set CPU_MODEL 32'h00068030 -set CPU_IMPLEMENTATION 32'h57463330 -set CPU_FEATURES 32'h0000000c astra_soc;"
    TG_GHDL="ghdl --std=08 -fsynopsys --latches $T/TG68K_Pack.vhd $T/TG68K_ALU.vhd $T/TG68KdotC_Kernel.vhd $W/tg68k_wrap.vhd -e tg68k_wrap;"
    ;;
  tg68k|tg68k_c)
    CPU_CORE="tg68k"
    YOSYS_CPU_PARAM="chparam -set CPU_TG68K 1 -set CPU_MODEL 32'h00068020 -set CPU_IMPLEMENTATION 32'h54473230 -set CPU_FEATURES 32'h0000000c astra_soc;"
    TG_GHDL="ghdl --std=08 -fsynopsys --latches $T/TG68K_Pack.vhd $T/TG68K_ALU.vhd $T/TG68KdotC_Kernel.vhd $W/tg68k_wrap.vhd -e tg68k_wrap;"
    ;;
  tg68k030|tg68k_mmu|tg68k_c_030_mmu)
    CPU_CORE="tg68k030"
    YOSYS_CPU_PARAM="chparam -set CPU_TG68K 1 -set CPU_MODEL 32'h00068030 -set CPU_IMPLEMENTATION 32'h54473330 -set CPU_FEATURES 32'h0000000d astra_soc;"
    TG_GHDL="ghdl --std=08 -fsynopsys --latches $T030/TG68K_Pack.vhd $T030/TG68K_ALU.vhd $T030/TG68K_PMMU_030.vhd $T030/TG68K_Cache_030.vhd $T030/TG68KdotC_Kernel.vhd $T030/TG68K.vhd $W/tg68k030_wrap.vhd -e tg68k_wrap;"
    ;;
  *)
    echo "unknown CPU_CORE='$CPU_CORE' (expected wf68k, tg68k, or tg68k030)" >&2
    exit 2
    ;;
esac
if [ "${UART_MONITOR:-0}" = "1" ]; then
  YOSYS_MONITOR_PARAM="chparam -set UART_MONITOR 1 astra_soc;"
fi
if [ "${SDRAM_ENABLE:-1}" = "0" ]; then
  YOSYS_SDRAM_PARAM="chparam -set SDRAM_ENABLE 0 astra_soc;"
fi
if [ "${HDMI_ENABLE:-1}" = "0" ]; then
  YOSYS_HDMI_PARAM="chparam -set HDMI_ENABLE 0 astra_soc;"
else
  PNR_HDMI_FLAGS="--pre-place place_hdmi_serializer.py"
fi
if [ -n "${CPU_CLK_DIV_BIT:-}" ]; then
  YOSYS_CLOCK_PARAM="chparam -set CPU_CLK_DIV_BIT $CPU_CLK_DIV_BIT astra_soc;"
fi
if [ -n "${SOC_BUILD_ID:-}" ]; then
  YOSYS_BUILD_PARAM="chparam -set SOC_BUILD_ID 32'h${SOC_BUILD_ID#0x} astra_soc;"
fi
cp "$1" rom_init.hex                 # ROM for this build (astra_soc.sv $readmemh is cwd-relative)
cp "$SOC/post_fonts.hex" post_fonts.hex
CORES="$C/wf68k30L_pkg.vhd $C/wf68k30L_address_registers.vhd $C/wf68k30L_data_registers.vhd \
$C/wf68k30L_alu.vhd $C/wf68k30L_opcode_decoder.vhd $C/wf68k30L_bus_interface.vhd \
$C/wf68k30L_exception_handler.vhd $C/wf68k30L_control.vhd $C/wf68k30L_top.vhd $W/wf68k_wrap.vhd"
yosys -m ghdl -p "
ghdl --std=08 -fsynopsys $CORES -e wf68k_wrap;
$TG_GHDL
read_verilog -sv -DSYNTHESIS -DLATTICE_ECP5 \
  $H/audio_clock_regeneration_packet.sv $H/audio_info_frame.sv \
  $H/audio_sample_packet.sv $H/auxiliary_video_information_info_frame.sv \
  $H/source_product_description_info_frame.sv $H/packet_assembler.sv \
  $H/packet_picker.sv $H/tmds_channel.sv $H/serializer.sv $H/hdmi.sv \
  $SOC/uart_tx.sv $SOC/uart_rx.sv $SOC/ecp5pll.sv \
  $SOC/thirdparty/core_sdram_axi4/sdram_axi_core.v \
  $SOC/sdram32_controller.sv $SOC/sdram32_cpu_bridge.sv $SOC/astraea_blitter.sv \
  $SOC/tg68k_cache_store.sv \
  $SOC/sdram32_bist.sv \
  $SOC/post_console.sv $SOC/astra_soc.sv;
$YOSYS_CPU_PARAM
$YOSYS_MONITOR_PARAM
$YOSYS_SDRAM_PARAM
$YOSYS_HDMI_PARAM
$YOSYS_CLOCK_PARAM
$YOSYS_BUILD_PARAM
proc; opt; scc -select; select -list; select -clear; scc -expect 0;
synth_ecp5 -top astra_soc $SYNTH_ECP5_FLAGS -json astra.json;
scc -expect 0;
" > "yosys_${TAG}.log" 2>&1
nextpnr-ecp5 --85k --package CABGA381 --freq "$TARGET_FREQ_MHZ" --seed "$PNR_SEED" $PNR_HDMI_FLAGS --json astra.json --lpf "$SOC/astra_soc.lpf" --textcfg astra.config > "pnr_${TAG}.log" 2>&1
echo "nextpnr rc=$? (0=routed; check yosys_${TAG}.log for loop warnings)"
ecppack astra.config astra.bit
echo "built astra.bit from $1 (CPU_CORE=$CPU_CORE UART_MONITOR=${UART_MONITOR:-0} SDRAM_ENABLE=${SDRAM_ENABLE:-1} HDMI_ENABLE=${HDMI_ENABLE:-1} CPU_CLK_DIV_BIT=${CPU_CLK_DIV_BIT:-2} TARGET_FREQ_MHZ=$TARGET_FREQ_MHZ PNR_SEED=$PNR_SEED SYNTH_ECP5_FLAGS='${SYNTH_ECP5_FLAGS}')"
