#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
QUARTUS_ROOT=${QUARTUS_ROOT:-/home/barry/altera_pro/26.1.1/quartus}
GHRD=$(realpath "${1:?usage: build_vendor_baseline.sh GHRD.qar [quartus-license]}")
LICENSE=$(realpath "${2:-/home/barry/.altera.quartus/quartus2_lic.dat}")
BUILD_ROOT="$ROOT/build/de25"
OUT="$BUILD_ROOT/vendor-ghrd"
INCOMING="$BUILD_ROOT/vendor-ghrd.incoming"
QUARTUS_BIN="$QUARTUS_ROOT/bin"
. "$ROOT/fpga/de25/build_common.sh"

python3 "$ROOT/fpga/de25/check_platform.py" \
    --quartus-root "$QUARTUS_ROOT" --ghrd "$GHRD" --license "$LICENSE"

trap 'rm -rf "$INCOMING"' EXIT
prepare_de25_tree "$INCOMING"
compile_de25_tree "$INCOMING"
record_de25_build "$INCOMING"
publish_de25_tree "$INCOMING" "$OUT"
trap - EXIT
echo "DE25 vendor baseline build: PASS"
