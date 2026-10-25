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
    rm -f "$mount/astra68.core.rbf.astra-new"
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
# Kernel installation owns its versioned Image/DTB selection and rollback.
script_hash=$(sha256sum "$mount/boot.scr.uimg" | awk '{print $1}')
cp "$bundle/astra68.core.rbf" "$mount/astra68.core.rbf.astra-new"
sync
[[ $(sha256sum "$mount/astra68.core.rbf.astra-new" | awk '{print $1}') == "$rbf_hash" ]]
mv "$mount/astra68.core.rbf.astra-new" "$mount/astra68.core.rbf"

sync

[[ $(sha256sum "$mount/astra68.core.rbf" | awk '{print $1}') == "$rbf_hash" ]]
[[ $(sha256sum "$mount/boot.scr.uimg" | awk '{print $1}') == "$script_hash" ]]
trap - EXIT
if [[ $mounted -eq 1 ]]; then
    umount "$mount"
fi
echo "DE25 Astra boot install: PASS rbf=$rbf_hash kernel-script=$script_hash"
