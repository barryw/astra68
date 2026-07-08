#!/bin/bash
# Astra 68 — the ONLY sanctioned way to put a harness build on the board.
# Builds firmware+bitstream from current source, flashes, then VERIFIES the running device
# reports the exact BUILD_ID that was just built. A stale/failed flash makes this FAIL LOUDLY.
# Logs every build to BUILD_LOG.md so a BUILD_ID always maps to its fixes/features.
#
# Usage:  bash sw/harte/build_flash.sh "short description of this build's fixes/features"
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
DESC="${1:-(no description)}"
source ~/oss-cad-suite/environment 2>/dev/null || true

echo "== [1/5] build firmware (regenerates build_id.h from RTL+fw hash) =="
make -s -C sw/harte clean
make -C sw/harte
EXPECT=$(make -s -C sw/harte id | grep -oE '[0-9a-f]{8}')
echo "    expected BUILD_ID = 0x$EXPECT"

echo "== [2/5] build bitstream =="
( cd fpga/soc/oss_flow && bash mkbit.sh ../../../sw/harte/rom_harness.hex harte >/dev/null )
BITSHA=$(sha1sum fpga/soc/oss_flow/astra.bit | cut -c1-12)
echo "    astra.bit sha=$BITSHA"

echo "== [3/5] flash (SPI + reconfig) =="
openFPGALoader --board ulx3s -f -r fpga/soc/oss_flow/astra.bit 2>&1 | tail -1
sleep 3

echo "== [4/5] verify running device == what we just built =="
GOT=$(python3 sw/harte/host/whatsloaded.py 2>/dev/null | grep -oE 'BUILD_ID = 0x[0-9a-f]{8}' | grep -oE '[0-9a-f]{8}$' || true)
echo "    device reports BUILD_ID = 0x${GOT:-NONE}"
if [ "${GOT:-}" != "$EXPECT" ]; then
    echo "!!! FAIL: device has 0x${GOT:-NONE}, expected 0x$EXPECT — STALE OR FAILED FLASH !!!"
    exit 1
fi

echo "== [5/5] log =="
GIT="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
[ -n "$(git status --porcelain 2>/dev/null)" ] && GIT="$GIT+dirty"
printf '`0x%s` | %s | bit:%s | %s | %s\n' "$EXPECT" "$GIT" "$BITSHA" "$(date -u +%Y-%m-%dT%H:%MZ)" "$DESC" >> sw/harte/BUILD_LOG.md
echo "OK — loaded & verified BUILD_ID 0x$EXPECT"
echo "----"
tail -1 sw/harte/BUILD_LOG.md
