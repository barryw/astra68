#!/bin/bash
# mkbit.sh <rom.hex> [tag] -> astra.bit
# Builds the canonical TG68K.C 68030/PMMU SoC via yosys+nextpnr.
# Packaging requires both a successful route and an explicit report timing gate.
set -e
[ "$#" -ge 1 ] || { echo "usage: $0 <rom.hex> [tag]" >&2; exit 2; }
[ -f "$1" ] || { echo "ROM image not found: $1" >&2; exit 2; }
ROM_INPUT="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
SYSTEM_ROM_INPUT=""
if [ -n "${ASTRA_SYSTEM_ROM:-}" ]; then
  [ -f "$ASTRA_SYSTEM_ROM" ] || {
    echo "system ROM image not found: $ASTRA_SYSTEM_ROM" >&2
    exit 2
  }
  SYSTEM_ROM_INPUT="$(cd "$(dirname "$ASTRA_SYSTEM_ROM")" && pwd)/$(basename "$ASTRA_SYSTEM_ROM")"
fi
# Locate oss-cad-suite: $OSS override, then common install dirs (Linux NUC, macOS brew).
if [ -z "${OSS:-}" ]; then
  for d in "$HOME/oss-cad-suite" /opt/homebrew/oss-cad-suite /opt/oss-cad-suite /usr/local/oss-cad-suite; do
    [ -d "$d" ] && OSS="$d" && break
  done
fi
[ -d "${OSS:-}" ] || { echo "oss-cad-suite not found (set \$OSS)"; exit 1; }
# shellcheck source=/dev/null
source "$OSS/environment"
export GHDL_PREFIX="$OSS/lib/ghdl"
cd "$(dirname "$0")"                 # fpga/soc/oss_flow/
REPO_ROOT="$(cd ../../.. && pwd)"
SOC=..                              # fpga/soc/
H=$SOC/hdmi                         # NovaVM-proven hdl-util HDMI pipeline
T030M2=../../cpu/tg68k_c_030_mmu2 # fpga/cpu/tg68k_c_030_mmu2/
W=../../cpu                        # fpga/cpu/ wrappers
TAG="${2:-build}"
case "$TAG" in
  ''|*[!A-Za-z0-9_.-]*)
    echo "tag accepts only letters, digits, dot, underscore, and dash" >&2
    exit 2
    ;;
esac
MANIFEST="build_${TAG}.manifest"
BITSTREAM_TMP=".astra_${TAG}.bit.tmp"
if [ -z "${SD_BOOT_ENABLE+x}" ]; then
  case "$(basename "$ROM_INPUT")" in
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
TARGET_FREQ_MHZ="${TARGET_FREQ_MHZ:-12.5}"
PNR_SEED="${PNR_SEED:-4}"
PNR_PLACER="${PNR_PLACER:-heap}"
PNR_ROUTER="${PNR_ROUTER:-router1}"
PNR_THREADS="${PNR_THREADS:-}"
PNR_TIMING_WEIGHT="${PNR_TIMING_WEIGHT:-20}"
# This design is dense enough that nextpnr's 10,000-attempt heap legalizer
# cutoff can reject a legal placement. Zero disables that internal cutoff;
# build orchestration remains responsible for a wall-clock timeout.
PNR_HEAP_CELL_PLACEMENT_TIMEOUT="${PNR_HEAP_CELL_PLACEMENT_TIMEOUT:-0}"
PNR_TIMING_RIPUP="${PNR_TIMING_RIPUP:-0}"
PNR_TIMING_ALLOW_FAIL="${PNR_TIMING_ALLOW_FAIL:-0}"
PNR_ROUTER2_ALT_WEIGHTS="${PNR_ROUTER2_ALT_WEIGHTS:-0}"
ASTRA_FLOORPLAN_MODE="${ASTRA_FLOORPLAN_MODE:-critical}"
ASTRA_FLOORPLAN_ENFORCE="${ASTRA_FLOORPLAN_ENFORCE:-host_io,astraea_blitter,astraea_blitter_cdc,astraea_blitter_control}"
RESOURCE_PROFILE="${RESOURCE_PROFILE:-kernel_platform_v1}"
UART_MONITOR="${UART_MONITOR:-0}"
SDRAM_ENABLE="${SDRAM_ENABLE:-1}"
HDMI_ENABLE="${HDMI_ENABLE:-1}"
USB_ENABLE="${USB_ENABLE:-1}"
CPU_CLK_DIV_BIT="${CPU_CLK_DIV_BIT:-0}"
UART_BAUD="${UART_BAUD:-115200}"
SOC_BUILD_ID="${SOC_BUILD_ID:-00000000}"
export ASTRA_FLOORPLAN_MODE ASTRA_FLOORPLAN_ENFORCE
YOSYS_MONITOR_PARAM=""
YOSYS_SDRAM_PARAM=""
YOSYS_HDMI_PARAM=""
YOSYS_USB_PARAM=""
YOSYS_CLOCK_PARAM=""
YOSYS_UART_PARAM=""
YOSYS_BUILD_PARAM=""
YOSYS_SD_BOOT_PARAM=""
YOSYS_ASTRA_HOST_PARAM=""
YOSYS_ROM_PARAM="chparam -set ROM_WORDS $EFFECTIVE_ROM_WORDS astra_soc;"
YOSYS_HIERARCHY_ATTR=""
PNR_HDMI_FLAGS=()
PNR_THREAD_FLAGS=()
PNR_PLACER_EFFORT_FLAGS=()
PNR_ROUTER_FLAGS=()
PNR_TIMING_FLAGS=()
PNR_TIMING_ALLOW_FLAGS=()
TG_GHDL="ghdl --std=08 -fsynopsys --latches $T030M2/TG68K_Pack.vhd $T030M2/TG68K_ALU.vhd $T030M2/TG68K_PMMU_030.vhd $T030M2/TG68K_Cache_030.vhd $T030M2/TG68KdotC_Kernel.vhd $T030M2/TG68K.vhd $W/tg68k030_mmu2_wrap.vhd -e tg68k_wrap;"
case "$PNR_PLACER" in
  heap|sa|static) ;;
  *) echo "PNR_PLACER must be heap, sa, or static" >&2; exit 2 ;;
esac
case "$PNR_ROUTER" in
  router1|router2) ;;
  *) echo "PNR_ROUTER must be router1 or router2" >&2; exit 2 ;;
esac
case "$PNR_TIMING_WEIGHT" in
  ''|*[!0-9]*) echo "PNR_TIMING_WEIGHT must be a nonnegative integer" >&2; exit 2 ;;
esac
case "$PNR_HEAP_CELL_PLACEMENT_TIMEOUT" in
  ''|*[!0-9]*)
    echo "PNR_HEAP_CELL_PLACEMENT_TIMEOUT must be a nonnegative integer" >&2
    exit 2
    ;;
esac
case "$PNR_SEED" in
  ''|*[!0-9]*) echo "PNR_SEED must be a nonnegative integer" >&2; exit 2 ;;
esac
case "$CPU_CLK_DIV_BIT" in
  0|1|2) ;;
  *) echo "CPU_CLK_DIV_BIT must be 0, 1, or 2" >&2; exit 2 ;;
esac
case "$UART_BAUD" in
  ''|*[!0-9]*|0) echo "UART_BAUD must be a positive integer" >&2; exit 2 ;;
esac
if [ -n "$PNR_THREADS" ]; then
  case "$PNR_THREADS" in
    *[!0-9]*|0) echo "PNR_THREADS must be a positive integer" >&2; exit 2 ;;
  esac
fi
if [[ ! "$TARGET_FREQ_MHZ" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   [[ "$TARGET_FREQ_MHZ" =~ ^0+([.]0+)?$ ]]; then
  echo "TARGET_FREQ_MHZ must be a positive number" >&2
  exit 2
fi
SOC_BUILD_ID_HEX="${SOC_BUILD_ID#0x}"
if [[ ! "$SOC_BUILD_ID_HEX" =~ ^[0-9A-Fa-f]{1,8}$ ]]; then
  echo "SOC_BUILD_ID must contain one to eight hexadecimal digits" >&2
  exit 2
fi
SOC_BUILD_ID="$SOC_BUILD_ID_HEX"
for toggle in "$UART_MONITOR" "$SDRAM_ENABLE" "$HDMI_ENABLE" "$USB_ENABLE" \
  "$SD_BOOT_ENABLE" "$ASTRA_HOST_ENABLE" "$PNR_TIMING_RIPUP" \
  "$PNR_TIMING_ALLOW_FAIL" "$PNR_ROUTER2_ALT_WEIGHTS"; do
  case "$toggle" in
    0|1) ;;
    *) echo "feature and PNR toggles must be 0 or 1" >&2; exit 2 ;;
  esac
done
python3 check_resource_budget.py --profile "$RESOURCE_PROFILE" \
  --validate-profile >/dev/null
if [ "$SD_BOOT_ENABLE" = "1" ] && [ "${SYNTH_ONLY:-0}" != "1" ] &&
   [ "$PNR_TIMING_ALLOW_FAIL" != "1" ] && [ -z "$SYSTEM_ROM_INPUT" ]; then
  echo "release SD boot requires ASTRA_SYSTEM_ROM=/path/to/ASTRA68.ROM" >&2
  exit 2
fi
if [ -n "$SYNTH_KEEP_HIERARCHY" ]; then
  case "$SYNTH_KEEP_HIERARCHY" in
    *[!A-Za-z0-9_[:space:]]*)
      echo "SYNTH_KEEP_HIERARCHY accepts whitespace-separated module names" >&2
      exit 2
      ;;
  esac
  YOSYS_HIERARCHY_ATTR="setattr -mod -set keep_hierarchy 1 $SYNTH_KEEP_HIERARCHY;"
fi
if [ "$UART_MONITOR" = "1" ]; then
  YOSYS_MONITOR_PARAM="chparam -set UART_MONITOR 1 astra_soc;"
fi
if [ "$SDRAM_ENABLE" = "0" ]; then
  YOSYS_SDRAM_PARAM="chparam -set SDRAM_ENABLE 0 astra_soc;"
fi
if [ "$HDMI_ENABLE" = "0" ]; then
  YOSYS_HDMI_PARAM="chparam -set HDMI_ENABLE 0 astra_soc;"
else
  PNR_HDMI_FLAGS=(--pre-place place_hdmi_serializer.py)
fi
if [ "$USB_ENABLE" = "0" ]; then
  YOSYS_USB_PARAM="chparam -set USB_ENABLE 0 astra_soc;"
fi
if [ -n "$PNR_THREADS" ]; then
  PNR_THREAD_FLAGS=(--threads "$PNR_THREADS")
fi
if [ "$PNR_PLACER" = "heap" ]; then
  PNR_PLACER_EFFORT_FLAGS=(
    --placer-heap-cell-placement-timeout "$PNR_HEAP_CELL_PLACEMENT_TIMEOUT"
  )
fi
if [ "$PNR_ROUTER2_ALT_WEIGHTS" = "1" ]; then
  [ "$PNR_ROUTER" = "router2" ] || {
    echo "PNR_ROUTER2_ALT_WEIGHTS=1 requires PNR_ROUTER=router2" >&2
    exit 2
  }
  PNR_ROUTER_FLAGS=(--router2-alt-weights)
fi
if [ "$PNR_TIMING_RIPUP" = "1" ]; then
  PNR_TIMING_FLAGS=(--tmg-ripup)
fi
if [ "$PNR_TIMING_ALLOW_FAIL" = "1" ]; then
  PNR_TIMING_ALLOW_FLAGS=(--timing-allow-fail)
fi
YOSYS_CLOCK_PARAM="chparam -set CPU_CLK_DIV_BIT $CPU_CLK_DIV_BIT astra_soc;"
YOSYS_UART_PARAM="chparam -set UART_BAUD $UART_BAUD astra_soc;"
YOSYS_BUILD_PARAM="chparam -set SOC_BUILD_ID 32'h$SOC_BUILD_ID astra_soc;"
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
# Never leave an old package looking like output from this invocation.
rm -f astra.bit "$BITSTREAM_TMP" "$MANIFEST"
ROM_INPUT_WORDS="$(awk 'NF && $1 !~ /^(@|\/\/|#)/ { count++ } END { print count + 0 }' "$ROM_INPUT")"
[ "$ROM_INPUT_WORDS" -le "$EFFECTIVE_ROM_WORDS" ] || {
  echo "ROM image has $ROM_INPUT_WORDS words but ROM_WORDS=$EFFECTIVE_ROM_WORDS" >&2
  exit 2
}
cp "$ROM_INPUT" rom_init.hex         # ROM for this build (astra_soc.sv $readmemh is cwd-relative)
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
  $SOC/astra_front_panel.sv $SOC/vesta_irq_timer.sv \
  $SOC/astra_host_async_byte_fifo.sv $SOC/astra_host_spi_slave.sv \
  $SOC/astra_async_fifo.sv $SOC/astra_host_runtime.sv \
  $SOC/astra_host_service.sv \
  $SOC/usb_ohci_ctrl_cdc.sv $SOC/usb_ohci_dma_bridge.sv \
  $SOC/thirdparty/usb_ohci/UsbOhciWishbone_Dw32_Pc1_Pf48000000.v \
  $SOC/usb_ohci_host.sv \
  $SOC/boot_memory_map.sv $SOC/ecp5pll.sv \
  $SOC/thirdparty/core_sdram_axi4/sdram_axi_core.v \
  $SOC/sdram32_controller.sv $SOC/sdram32_cpu_bridge.sv \
  $SOC/astraea_blitter.sv $SOC/astraea_pixel_port.sv $SOC/astraea_draw.sv \
  $SOC/astraea_copper.sv $SOC/astraea_chip.sv \
  $SOC/vega_sprite_builder.sv $SOC/vega_video.sv \
  $SOC/tg68k_cache_store.sv \
  $SOC/sdram32_bist.sv \
  $SOC/post_console.sv $SOC/astra_soc.sv;
$YOSYS_MONITOR_PARAM
$YOSYS_SDRAM_PARAM
$YOSYS_HDMI_PARAM
$YOSYS_USB_PARAM
$YOSYS_CLOCK_PARAM
$YOSYS_UART_PARAM
$YOSYS_BUILD_PARAM
$YOSYS_SD_BOOT_PARAM
$YOSYS_ASTRA_HOST_PARAM
$YOSYS_ROM_PARAM
$YOSYS_HIERARCHY_ATTR
# The stock check stage runs autoname over the entire mapped netlist. That is
# cosmetic for JSON/nextpnr and becomes prohibitively expensive at this SoC's
# size, so run the substantive checks explicitly and keep internal names. Do
# not run proc/opt before synth_ecp5: its begin stage loads the ECP5 primitive
# library, and an earlier opt pass would otherwise discard the input-only GSR
# instance before lattice_gsr can enable configuration-time reset on the FFs.
synth_ecp5 -top astra_soc $SYNTH_ECP5_FLAGS -run begin:check;
# lattice_gsr resolves AUTO per RTL module. The reset-release synchronizers
# intentionally retain hierarchy, but the ECP5 GSR itself is device-global.
# Enable those mapped FFs explicitly while retaining the one legal top-level
# GSR primitive; setparam mutates cells, unlike chparam's module re-elaboration.
setparam -set GSR \"ENABLED\" t:TRELLIS_FF;
hierarchy -check;
stat;
check -noinit;
select -assert-count 32 astra_soc/c:g_build_id_lut*.build_id_lut_i;
blackbox =A:whitebox;
scc -select; select -list; select -clear;
scc -expect 0;
write_json astra.json;
" > "$YOSYS_LOG" 2>&1
python3 check_por.py astra.json
if [ "$HDMI_ENABLE" = "1" ]; then
  python3 check_post_font_rom.py astra.json
fi
if [ "$ASTRA_HOST_ENABLE" = "1" ]; then
  # shellcheck disable=SC2016
  grep -Fq 'mapping memory astra_soc.g_sdram_enabled.g_astra_host.host_spi_i.rx_fifo.mem via $__DP16KD_' "$YOSYS_LOG" || {
    echo "AstraHost RX FIFO was not mapped to ECP5 block RAM" >&2
    exit 1
  }
  # shellcheck disable=SC2016
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
  echo "synthesized astra.json from $ROM_INPUT (tag=$TAG); SYNTH_ONLY=1 skips place/route"
  exit 0
fi
PLACED_JSON="placed_${TAG}.json"
ROUTE_INPUT_JSON="route_input_${TAG}.json"
ROUTED_JSON="routed_${TAG}.json"
PLACE_REPORT="pnr_place_${TAG}.json"
PLACE_LOG="pnr_place_${TAG}.log"
ROUTE_REPORT="pnr_${TAG}.json"
ROUTE_LOG="pnr_${TAG}.log"
rm -f "$PLACED_JSON" "$ROUTE_INPUT_JSON" "$ROUTED_JSON" \
  "$PLACE_REPORT" "$ROUTE_REPORT" astra.config

# Placement estimates fail at this density even for timing-clean routes. The
# placement-only waiver preserves the placed artifact; final routing below has
# no waiver unless the caller explicitly requests a diagnostic-only report.
nextpnr-ecp5 --85k --package CABGA381 --freq "$TARGET_FREQ_MHZ" \
  --seed "$PNR_SEED" --placer "$PNR_PLACER" \
  --placer-heap-timingweight "$PNR_TIMING_WEIGHT" \
  "${PNR_PLACER_EFFORT_FLAGS[@]}" \
  "${PNR_THREAD_FLAGS[@]}" "${PNR_HDMI_FLAGS[@]}" \
  --json astra.json --lpf "$SOC/astra_soc.lpf" --sdc astra_clocks.sdc \
  --no-route --timing-allow-fail --write "$PLACED_JSON" \
  --report "$PLACE_REPORT" > "$PLACE_LOG" 2>&1

# nextpnr serializes the placement-only waiver and default router into its JSON.
# Remove the stale router so PNR_ROUTER is honored. Production also clears the
# waiver; diagnostic mode retains it explicitly. The serialized seed is the
# post-placement RNG state and is intentionally retained.
ROUTE_PREP_FLAGS=()
if [ "$PNR_TIMING_ALLOW_FAIL" = "1" ]; then
  ROUTE_PREP_FLAGS=(--keep-timing-waiver)
fi
python3 prepare_route_input.py "${ROUTE_PREP_FLAGS[@]}" \
  "$PLACED_JSON" "$ROUTE_INPUT_JSON"

nextpnr-ecp5 --85k --package CABGA381 --freq "$TARGET_FREQ_MHZ" \
  --router "$PNR_ROUTER" \
  "${PNR_ROUTER_FLAGS[@]}" "${PNR_THREAD_FLAGS[@]}" \
  "${PNR_TIMING_FLAGS[@]}" "${PNR_TIMING_ALLOW_FLAGS[@]}" \
  --no-pack --no-place --json "$ROUTE_INPUT_JSON" \
  --lpf "$SOC/astra_soc.lpf" --sdc astra_clocks.sdc \
  --pre-route refresh_ecp5_lutperm.py \
  --write "$ROUTED_JSON" --textcfg astra.config --report "$ROUTE_REPORT" \
  > "$ROUTE_LOG" 2>&1
echo "nextpnr split route complete; check yosys_${TAG}.log and $ROUTE_LOG"
python3 check_ecp5_lut_permutation.py "$ROUTED_JSON"
if [ "$PNR_TIMING_ALLOW_FAIL" = "1" ]; then
  python3 check_resource_budget.py --profile "$RESOURCE_PROFILE" "$ROUTE_REPORT"
  echo "diagnostic route complete; PNR_TIMING_ALLOW_FAIL=1 suppresses bitstream packaging"
  exit 0
fi
python3 check_timing.py "$ROUTE_REPORT"
python3 check_resource_budget.py --profile "$RESOURCE_PROFILE" "$ROUTE_REPORT"
ecppack astra.config "$BITSTREAM_TMP"
mv "$BITSTREAM_TMP" astra.bit

SOURCE_REVISION="${ASTRA_SOURCE_REVISION:-$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || printf unknown)}"
{
  printf 'source_revision=%s\n' "$SOURCE_REVISION"
  printf 'host=%s\n' "$(hostname)"
  printf 'build_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'yosys=%s\n' "$(yosys -V)"
  printf 'nextpnr=%s\n' "$(nextpnr-ecp5 --version)"
  printf 'm68k_gcc=%s\n' "$(m68k-linux-gnu-gcc --version | sed -n '1p')"
  printf 'ghdl=%s\n' "$(ghdl --version | sed -n '1p')"
  printf 'ecp5_split_route_lutperm_refresh=enabled\n'
  printf 'system_rom=%s\n' "${SYSTEM_ROM_INPUT:-none}"
  printf 'CPU=TG68K030_MMU2 CPU_CLK_DIV_BIT=%s SDRAM_ENABLE=%s HDMI_ENABLE=%s USB_ENABLE=%s SD_BOOT_ENABLE=%s ASTRA_HOST_ENABLE=%s ROM_WORDS=%s UART_MONITOR=%s UART_BAUD=%s SOC_BUILD_ID=%s TARGET_FREQ_MHZ=%s PNR_SEED=%s PNR_PLACER=%s PNR_ROUTER=%s PNR_TIMING_WEIGHT=%s PNR_HEAP_CELL_PLACEMENT_TIMEOUT=%s PNR_TIMING_RIPUP=%s PNR_TIMING_ALLOW_FAIL=%s PNR_ROUTER2_ALT_WEIGHTS=%s PNR_THREADS=%s SYNTH_ECP5_FLAGS=%s SYNTH_KEEP_HIERARCHY=%s ASTRA_FLOORPLAN_MODE=%s ASTRA_FLOORPLAN_ENFORCE=%s RESOURCE_PROFILE=%s\n' \
    "$CPU_CLK_DIV_BIT" "$SDRAM_ENABLE" "$HDMI_ENABLE" "$USB_ENABLE" \
    "$SD_BOOT_ENABLE" "$ASTRA_HOST_ENABLE" "$EFFECTIVE_ROM_WORDS" "$UART_MONITOR" \
    "$UART_BAUD" "$SOC_BUILD_ID" "$TARGET_FREQ_MHZ" "$PNR_SEED" \
    "$PNR_PLACER" "$PNR_ROUTER" "$PNR_TIMING_WEIGHT" \
    "$PNR_HEAP_CELL_PLACEMENT_TIMEOUT" \
    "$PNR_TIMING_RIPUP" "$PNR_TIMING_ALLOW_FAIL" \
    "$PNR_ROUTER2_ALT_WEIGHTS" \
    "${PNR_THREADS:-default}" "$SYNTH_ECP5_FLAGS" \
    "${SYNTH_KEEP_HIERARCHY:-none}" "$ASTRA_FLOORPLAN_MODE" \
    "$ASTRA_FLOORPLAN_ENFORCE" "$RESOURCE_PROFILE"
  MANIFEST_FILES=("$ROM_INPUT" astra.json "$PLACED_JSON" "$ROUTED_JSON"
    "$ROUTE_INPUT_JSON" "$PLACE_REPORT" "$ROUTE_REPORT" astra.config astra.bit)
  if [ -n "$SYSTEM_ROM_INPUT" ]; then
    MANIFEST_FILES+=("$SYSTEM_ROM_INPUT")
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${MANIFEST_FILES[@]}"
  else
    shasum -a 256 "${MANIFEST_FILES[@]}"
  fi
} > "$MANIFEST"
echo "built astra.bit from $ROM_INPUT; manifest=$MANIFEST"
