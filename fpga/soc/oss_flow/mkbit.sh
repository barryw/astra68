#!/bin/bash
# mkbit.sh <rom.hex> [tag] -> astra.bit
# Builds the canonical TG68K.C 68030/PMMU SoC via yosys+nextpnr.
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
T030M2=../../cpu/tg68k_c_030_mmu2 # fpga/cpu/tg68k_c_030_mmu2/
W=../../cpu                        # fpga/cpu/ wrappers
TAG="${2:-build}"
if [ -z "${SD_BOOT_ENABLE+x}" ]; then
  case "$(basename "$1")" in
    stage0*.hex) SD_BOOT_ENABLE=1 ;;
    *) SD_BOOT_ENABLE=0 ;;
  esac
fi
if [ -z "${ASTRA_HOST_ENABLE+x}" ]; then
  ASTRA_HOST_ENABLE="$SD_BOOT_ENABLE"
fi
if [ -n "${ROM_WORDS:-}" ]; then
  EFFECTIVE_ROM_WORDS="$ROM_WORDS"
elif [ "$ASTRA_HOST_ENABLE" = "1" ]; then
  EFFECTIVE_ROM_WORDS=1024
elif [ "$SD_BOOT_ENABLE" = "1" ]; then
  EFFECTIVE_ROM_WORDS=2048
else
  EFFECTIVE_ROM_WORDS=65536
fi
case "$EFFECTIVE_ROM_WORDS" in
  ''|*[!0-9]*)
    echo "ROM_WORDS must be a positive integer" >&2
    exit 2
    ;;
  0)
    echo "ROM_WORDS must be a positive integer" >&2
    exit 2
    ;;
esac
SYNTH_ECP5_FLAGS="${SYNTH_ECP5_FLAGS:--abc2}"
SYNTH_KEEP_HIERARCHY="${SYNTH_KEEP_HIERARCHY:-}"
TARGET_FREQ_MHZ="${TARGET_FREQ_MHZ:-12}"
PNR_SEED="${PNR_SEED:-2}"
PNR_ROUTER="${PNR_ROUTER:-router1}"
PNR_THREADS="${PNR_THREADS:-}"
PNR_ROUTER2_ALT_WEIGHTS="${PNR_ROUTER2_ALT_WEIGHTS:-0}"
YOSYS_MONITOR_PARAM=""
YOSYS_SDRAM_PARAM=""
YOSYS_HDMI_PARAM=""
YOSYS_CLOCK_PARAM=""
YOSYS_UART_PARAM=""
YOSYS_BUILD_PARAM=""
YOSYS_SD_BOOT_PARAM=""
YOSYS_ASTRA_HOST_PARAM=""
YOSYS_ROM_PARAM="chparam -set ROM_WORDS $EFFECTIVE_ROM_WORDS astra_soc;"
YOSYS_HIERARCHY_ATTR=""
PNR_HDMI_FLAGS=""
PNR_THREAD_FLAGS=""
PNR_ROUTER_FLAGS=""
TG_GHDL="ghdl --std=08 -fsynopsys --latches $T030M2/TG68K_Pack.vhd $T030M2/TG68K_ALU.vhd $T030M2/TG68K_PMMU_030.vhd $T030M2/TG68K_Cache_030.vhd $T030M2/TG68KdotC_Kernel.vhd $T030M2/TG68K.vhd $W/tg68k030_mmu2_wrap.vhd -e tg68k_wrap;"
if [ -n "$SYNTH_KEEP_HIERARCHY" ]; then
  case "$SYNTH_KEEP_HIERARCHY" in
    *[!A-Za-z0-9_[:space:]]*)
      echo "SYNTH_KEEP_HIERARCHY accepts whitespace-separated module names" >&2
      exit 2
      ;;
  esac
  YOSYS_HIERARCHY_ATTR="setattr -mod -set keep_hierarchy 1 $SYNTH_KEEP_HIERARCHY;"
fi
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
if [ -n "$PNR_THREADS" ]; then
  PNR_THREAD_FLAGS="--threads $PNR_THREADS"
fi
if [ "$PNR_ROUTER2_ALT_WEIGHTS" = "1" ]; then
  [ "$PNR_ROUTER" = "router2" ] || {
    echo "PNR_ROUTER2_ALT_WEIGHTS=1 requires PNR_ROUTER=router2" >&2
    exit 2
  }
  PNR_ROUTER_FLAGS="--router2-alt-weights"
fi
if [ -n "${CPU_CLK_DIV_BIT:-}" ]; then
  YOSYS_CLOCK_PARAM="chparam -set CPU_CLK_DIV_BIT $CPU_CLK_DIV_BIT astra_soc;"
fi
if [ -n "${UART_BAUD:-}" ]; then
  YOSYS_UART_PARAM="chparam -set UART_BAUD $UART_BAUD astra_soc;"
fi
if [ -n "${SOC_BUILD_ID:-}" ]; then
  YOSYS_BUILD_PARAM="chparam -set SOC_BUILD_ID 32'h${SOC_BUILD_ID#0x} astra_soc;"
fi
if [ "$SD_BOOT_ENABLE" = "1" ]; then
  [ "${SDRAM_ENABLE:-1}" != "0" ] || {
    echo "SD boot requires SDRAM_ENABLE=1" >&2
    exit 2
  }
  YOSYS_SD_BOOT_PARAM="chparam -set SD_BOOT_ENABLE 1 astra_soc;"
fi
if [ "$ASTRA_HOST_ENABLE" = "1" ]; then
  [ "$SD_BOOT_ENABLE" = "1" ] || {
    echo "ASTRA_HOST_ENABLE=1 requires SD_BOOT_ENABLE=1" >&2
    exit 2
  }
  YOSYS_ASTRA_HOST_PARAM="chparam -set ASTRA_HOST_ENABLE 1 astra_soc;"
fi
ROM_INPUT_WORDS="$(awk 'NF && $1 !~ /^(@|\/\/|#)/ { count++ } END { print count + 0 }' "$1")"
[ "$ROM_INPUT_WORDS" -le "$EFFECTIVE_ROM_WORDS" ] || {
  echo "ROM image has $ROM_INPUT_WORDS words but ROM_WORDS=$EFFECTIVE_ROM_WORDS" >&2
  exit 2
}
cp "$1" rom_init.hex                 # ROM for this build (astra_soc.sv $readmemh is cwd-relative)
cp "$SOC/post_fonts.hex" post_fonts.hex
YOSYS_LOG="yosys_${TAG}.log"
yosys -m ghdl -p "
$TG_GHDL
read_verilog -sv -DSYNTHESIS -DLATTICE_ECP5 \
  $H/audio_clock_regeneration_packet.sv $H/audio_info_frame.sv \
  $H/audio_sample_packet.sv $H/auxiliary_video_information_info_frame.sv \
  $H/source_product_description_info_frame.sv $H/packet_assembler.sv \
  $H/packet_picker.sv $H/tmds_channel.sv $H/serializer.sv $H/hdmi.sv \
  $SOC/uart_tx.sv $SOC/uart_rx.sv $SOC/uart_rx_fifo.sv $SOC/spi_sd.sv \
  $SOC/astra_front_panel.sv \
  $SOC/astra_host_async_byte_fifo.sv $SOC/astra_host_spi_slave.sv \
  $SOC/astra_host_boot.sv \
  $SOC/boot_memory_map.sv $SOC/ecp5pll.sv \
  $SOC/thirdparty/core_sdram_axi4/sdram_axi_core.v \
  $SOC/sdram32_controller.sv $SOC/sdram32_cpu_bridge.sv \
  $SOC/astraea_blitter.sv $SOC/astraea_pixel_port.sv $SOC/astraea_draw.sv \
  $SOC/astraea_copper.sv $SOC/astraea_chip.sv \
  $SOC/vega_tile_builder.sv $SOC/vega_sprite_builder.sv $SOC/vega_video.sv \
  $SOC/tg68k_cache_store.sv \
  $SOC/sdram32_bist.sv \
  $SOC/post_console.sv $SOC/astra_soc.sv;
$YOSYS_MONITOR_PARAM
$YOSYS_SDRAM_PARAM
$YOSYS_HDMI_PARAM
$YOSYS_CLOCK_PARAM
$YOSYS_UART_PARAM
$YOSYS_BUILD_PARAM
$YOSYS_SD_BOOT_PARAM
$YOSYS_ASTRA_HOST_PARAM
$YOSYS_ROM_PARAM
$YOSYS_HIERARCHY_ATTR
proc; opt; scc -select; select -list; select -clear; scc -expect 0;
# The stock check stage runs autoname over the entire mapped netlist. That is
# cosmetic for JSON/nextpnr and becomes prohibitively expensive at this SoC's
# size, so run the substantive checks explicitly and keep internal names.
synth_ecp5 -top astra_soc $SYNTH_ECP5_FLAGS -run begin:check;
hierarchy -check;
stat;
check -noinit;
blackbox =A:whitebox;
write_json astra.json;
scc -expect 0;
" > "$YOSYS_LOG" 2>&1
if [ "$ASTRA_HOST_ENABLE" = "1" ]; then
  grep -Fq 'mapping memory astra_soc.g_sdram_enabled.g_astra_host.host_spi_i.rx_fifo.mem via $__DP16KD_' "$YOSYS_LOG" || {
    echo "AstraHost RX FIFO was not mapped to ECP5 block RAM" >&2
    exit 1
  }
  grep -Fq 'mapping memory astra_soc.g_sdram_enabled.g_astra_host.host_spi_i.tx_fifo.mem via $__DP16KD_' "$YOSYS_LOG" || {
    echo "AstraHost TX FIFO was not mapped to ECP5 block RAM" >&2
    exit 1
  }
  HOST_PAD_COUNT="$(awk '$2 == "TRELLIS_IO" { count = $1 } END { print count + 0 }' "$YOSYS_LOG")"
  [ "$HOST_PAD_COUNT" -ge 6 ] || {
    echo "AstraHost shared-SPI pad cells were removed during synthesis" >&2
    exit 1
  }
  if grep -Eq 'Handling (const CLK|always-active ARST).*host_spi_i' "$YOSYS_LOG"; then
    echo "AstraHost external SPI clock/select path became constant" >&2
    exit 1
  fi
fi
if [ "${SYNTH_ONLY:-0}" = "1" ]; then
  echo "synthesized astra.json from $1 (tag=$TAG); SYNTH_ONLY=1 skips place/route"
  exit 0
fi
nextpnr-ecp5 --85k --package CABGA381 --freq "$TARGET_FREQ_MHZ" \
  --seed "$PNR_SEED" --router "$PNR_ROUTER" $PNR_ROUTER_FLAGS $PNR_THREAD_FLAGS \
  $PNR_HDMI_FLAGS --json astra.json --lpf "$SOC/astra_soc.lpf" \
  --sdc astra_clocks.sdc \
  --textcfg astra.config --report "pnr_${TAG}.json" \
  > "pnr_${TAG}.log" 2>&1
echo "nextpnr rc=$? (0=routed; check yosys_${TAG}.log for loop warnings)"
python3 check_resource_budget.py "pnr_${TAG}.json"
ecppack astra.config astra.bit
echo "built astra.bit from $1 (CPU=TG68K030_MMU2 SD_BOOT_ENABLE=$SD_BOOT_ENABLE ASTRA_HOST_ENABLE=$ASTRA_HOST_ENABLE ROM_WORDS=$EFFECTIVE_ROM_WORDS UART_MONITOR=${UART_MONITOR:-0} SDRAM_ENABLE=${SDRAM_ENABLE:-1} HDMI_ENABLE=${HDMI_ENABLE:-1} CPU_CLK_DIV_BIT=${CPU_CLK_DIV_BIT:-2} UART_BAUD=${UART_BAUD:-115200} TARGET_FREQ_MHZ=$TARGET_FREQ_MHZ PNR_SEED=$PNR_SEED PNR_ROUTER=$PNR_ROUTER PNR_ROUTER2_ALT_WEIGHTS=$PNR_ROUTER2_ALT_WEIGHTS PNR_THREADS=${PNR_THREADS:-default} SYNTH_ECP5_FLAGS='${SYNTH_ECP5_FLAGS}' SYNTH_KEEP_HIERARCHY='${SYNTH_KEEP_HIERARCHY}')"
