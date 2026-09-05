#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$script_dir/../.." && pwd)
shell=${1:-$root/build/de25/astra-shell}
out=$root/build/de25/boot
incoming=$out.incoming

test -s "$shell/output_files/golden_top_boot.core.rbf"
test -s "$shell/output_files/astra68.hps.jic"
(cd "$shell" && sha256sum -c BUILD_SHA256SUMS)
command -v mkimage >/dev/null
command -v dumpimage >/dev/null

rm -rf "$incoming"
trap 'rm -rf "$incoming"' EXIT
mkdir -p "$incoming"

cp "$shell/output_files/golden_top_boot.core.rbf" \
    "$incoming/astra68.core.rbf"
cp "$shell/output_files/astra68.hps.jic" "$incoming/astra68.hps.jic"
(cd "$script_dir" && mkimage -f boot.its "$incoming/boot.scr.uimg")
dumpimage -l "$incoming/boot.scr.uimg" | grep -Fq 'Type:         Script'

python3 - "$script_dir/platform.json" >"$incoming/EXPECTED_DTB_SHA256" <<'PY'
import json
import sys

print(json.load(open(sys.argv[1], encoding="utf-8"))["boot_dtb_sha256"])
PY
cp "$script_dir/install_boot_bundle.sh" "$incoming/install_boot_bundle.sh"
cp "$script_dir/program_hps_qspi.sh" "$incoming/program_hps_qspi.sh"
cp "$shell/BUILD_SHA256SUMS" "$incoming/SOURCE_SHA256SUMS"
(
    cd "$incoming"
    sha256sum astra68.core.rbf astra68.hps.jic boot.scr.uimg \
        EXPECTED_DTB_SHA256 install_boot_bundle.sh program_hps_qspi.sh \
        SOURCE_SHA256SUMS >SHA256SUMS
    sha256sum -c SHA256SUMS
)

rm -rf "$out"
mv "$incoming" "$out"
trap - EXIT
(cd "$out" && sha256sum -c SHA256SUMS)
echo "DE25 Astra boot bundle: PASS $out"
