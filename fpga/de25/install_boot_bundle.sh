#!/usr/bin/env bash
set -euo pipefail

if [[ $(id -u) -ne 0 ]]; then
    echo "install_boot_bundle.sh must run as root" >&2
    exit 1
fi

bundle=$(cd "$(dirname "$0")" && pwd)
device=${1:-/dev/mmcblk0p1}
mount=${2:-/mnt/astra-boot}
mounted=0

cleanup() {
    status=$?
    rm -f "$mount/astra68.core.rbf.astra-new" \
        "$mount/boot.scr.uimg.astra-new"
    sync
    if [[ $mounted -eq 1 ]]; then
        umount "$mount" || true
    fi
    exit "$status"
}
trap cleanup EXIT

(cd "$bundle" && sha256sum -c SHA256SUMS)
mkdir -p "$mount"
if mountpoint -q "$mount"; then
    [[ $(findmnt -n -o SOURCE --target "$mount") == "$device" ]]
else
    mount "$device" "$mount"
    mounted=1
fi

expected_dtb=$(<"$bundle/EXPECTED_DTB_SHA256")
actual_dtb=$(sha256sum "$mount/socfpga_agilex5_de25_nano.dtb" | awk '{print $1}')
if [[ $actual_dtb != "$expected_dtb" ]]; then
    echo "refusing unqualified DE25 device tree" >&2
    echo "expected $expected_dtb" >&2
    echo "actual   $actual_dtb" >&2
    exit 1
fi

rbf_hash=$(sha256sum "$bundle/astra68.core.rbf" | awk '{print $1}')
script_hash=$(sha256sum "$bundle/boot.scr.uimg" | awk '{print $1}')
cp "$bundle/astra68.core.rbf" "$mount/astra68.core.rbf.astra-new"
sync
[[ $(sha256sum "$mount/astra68.core.rbf.astra-new" | awk '{print $1}') == "$rbf_hash" ]]
mv "$mount/astra68.core.rbf.astra-new" "$mount/astra68.core.rbf"

# Activate the script last: the old script ignores the new RBF, while the new
# script cannot become visible until its required RBF is complete.
cp "$bundle/boot.scr.uimg" "$mount/boot.scr.uimg.astra-new"
sync
[[ $(sha256sum "$mount/boot.scr.uimg.astra-new" | awk '{print $1}') == "$script_hash" ]]
mv "$mount/boot.scr.uimg.astra-new" "$mount/boot.scr.uimg"
sync

[[ $(sha256sum "$mount/astra68.core.rbf" | awk '{print $1}') == "$rbf_hash" ]]
[[ $(sha256sum "$mount/boot.scr.uimg" | awk '{print $1}') == "$script_hash" ]]
trap - EXIT
if [[ $mounted -eq 1 ]]; then
    umount "$mount"
fi
echo "DE25 Astra boot install: PASS rbf=$rbf_hash script=$script_hash"
