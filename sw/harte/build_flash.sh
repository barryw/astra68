#!/bin/bash
# Astra 68 - the sanctioned way to put a harness build on the board.
# Builds firmware+bitstream from current source, loads it, then verifies the running device
# reports the exact BUILD_ID that was just built. SRAM is the default so SPI POST survives.
# Logs every build to BUILD_LOG.md so a BUILD_ID always maps to its fixes/features.
#
# Usage:  bash sw/harte/build_flash.sh "short description of this build's fixes/features"
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
DESC="${1:-(no description)}"
CPU_CORE="${CPU_CORE:-tg68k030_mmu2}"
CPU_CLK_DIV_BIT="${CPU_CLK_DIV_BIT:-0}"
SDRAM_ENABLE="${SDRAM_ENABLE:-1}"
HDMI_ENABLE="${HDMI_ENABLE:-1}"
UART_BAUD="${UART_BAUD:-460800}"
TARGET_FREQ_MHZ="${TARGET_FREQ_MHZ:-12.5}"
PNR_SEED="${PNR_SEED:-4}"
FLASH_MODE="${FLASH_MODE:-sram}"
BUILD_ARGS=(
  "CPU_CORE=$CPU_CORE"
  "CPU_CLK_DIV_BIT=$CPU_CLK_DIV_BIT"
  "SDRAM_ENABLE=$SDRAM_ENABLE"
  "HDMI_ENABLE=$HDMI_ENABLE"
  "UART_BAUD=$UART_BAUD"
  "TARGET_FREQ_MHZ=$TARGET_FREQ_MHZ"
  "PNR_SEED=$PNR_SEED"
)
source ~/oss-cad-suite/environment 2>/dev/null || true

echo "== [1/5] build firmware (regenerates build_id.h from RTL+fw hash) =="
make -s -C sw/harte clean
make -C sw/harte "${BUILD_ARGS[@]}"
EXPECT=$(make -s -C sw/harte id "${BUILD_ARGS[@]}" | grep -oE '[0-9a-f]{8}')
echo "    expected BUILD_ID = 0x$EXPECT"
echo "    CPU_CORE=$CPU_CORE CPU_CLK_DIV_BIT=$CPU_CLK_DIV_BIT SDRAM_ENABLE=$SDRAM_ENABLE HDMI_ENABLE=$HDMI_ENABLE UART_BAUD=$UART_BAUD TARGET_FREQ_MHZ=$TARGET_FREQ_MHZ PNR_SEED=$PNR_SEED"

echo "== [2/5] build bitstream =="
( cd fpga/soc/oss_flow && \
  CPU_CLK_DIV_BIT="$CPU_CLK_DIV_BIT" SDRAM_ENABLE="$SDRAM_ENABLE" \
  HDMI_ENABLE="$HDMI_ENABLE" \
  UART_BAUD="$UART_BAUD" \
  TARGET_FREQ_MHZ="$TARGET_FREQ_MHZ" \
  PNR_SEED="$PNR_SEED" \
  SOC_BUILD_ID="$EXPECT" \
  SD_BOOT_ENABLE=0 bash mkbit.sh ../../../sw/harte/rom_harness.hex harte "$CPU_CORE" >/dev/null )
BITSHA=$(sha256sum fpga/soc/oss_flow/astra.bit | cut -d' ' -f1)
echo "    astra.bit sha=$BITSHA"

echo "== [3/5] load ($FLASH_MODE) =="
case "$FLASH_MODE" in
  sram) openFPGALoader --board ulx3s fpga/soc/oss_flow/astra.bit 2>&1 | tail -1 ;;
  spi)  openFPGALoader --board ulx3s -f -r fpga/soc/oss_flow/astra.bit 2>&1 | tail -1 ;;
  *) echo "unsupported FLASH_MODE '$FLASH_MODE' (expected sram or spi)" >&2; exit 2 ;;
esac
sleep 3

echo "== [4/5] verify running device == what we just built =="
GOT=$(ASTRA_BAUD="$UART_BAUD" python3 sw/harte/host/whatsloaded.py 2>/dev/null | grep -oE 'BUILD_ID = 0x[0-9a-f]{8}' | grep -oE '[0-9a-f]{8}$' || true)
echo "    device reports BUILD_ID = 0x${GOT:-NONE}"
if [ "${GOT:-}" != "$EXPECT" ]; then
    echo "!!! FAIL: device has 0x${GOT:-NONE}, expected 0x$EXPECT — STALE OR FAILED FLASH !!!"
    exit 1
fi

echo "== [5/5] log =="
GIT="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
[ -n "$(git status --porcelain 2>/dev/null)" ] && GIT="$GIT+dirty"
printf '`0x%s` | %s | sha256:%s | %s | %s [%s]\n' \
  "$EXPECT" "$GIT" "$BITSHA" "$(date -u +%Y-%m-%dT%H:%MZ)" "$DESC" \
  "CPU_CORE=$CPU_CORE CPU_CLK_DIV_BIT=$CPU_CLK_DIV_BIT SDRAM_ENABLE=$SDRAM_ENABLE HDMI_ENABLE=$HDMI_ENABLE UART_BAUD=$UART_BAUD TARGET_FREQ_MHZ=$TARGET_FREQ_MHZ PNR_SEED=$PNR_SEED FLASH_MODE=$FLASH_MODE" \
  >> sw/harte/BUILD_LOG.md
echo "OK — loaded & verified BUILD_ID 0x$EXPECT"
echo "----"
tail -1 sw/harte/BUILD_LOG.md
