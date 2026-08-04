#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
out_dir=${ASTRA_ARTY_OUT:-"$repo_root/build/arty-720p"}
boot_inputs=${ASTRA_ARTY_BOOT_INPUTS:-/mnt/Documents/astra68/platform/arty/linux-boot/20260723}
bootgen=${BOOTGEN:-bootgen}
bitstream=${ASTRA_ARTY_BITSTREAM:-"$out_dir/astra_arty_720p.bit"}
fsbl=${ASTRA_ARTY_FSBL:-"$boot_inputs/zynq_fsbl.elf"}
u_boot=${ASTRA_ARTY_UBOOT:-"$boot_inputs/u-boot.elf"}
dtb=${ASTRA_ARTY_DTB:-"$boot_inputs/system.dtb"}
fsbl_hash=${ASTRA_ARTY_FSBL_SHA256:-002d116f153b5b5eb439f5c5aa8693358ac409666ca67ceb43928c1b1efd8cd7}
u_boot_hash=${ASTRA_ARTY_UBOOT_SHA256:-c3797d01bbf40f8cf404f27a90a50a279ec431463d11cbcfee04c2916af5a5f1}
dtb_hash=${ASTRA_ARTY_DTB_SHA256:-f0348d6122d6604a3c12c944219d7d23f481c27ca9d1254dfd30e6bb0972addd}
bitstream_hash=${ASTRA_ARTY_BITSTREAM_SHA256:-}

check_hash() {
    local expected=$1
    local file=$2
    local actual
    actual=$(sha256sum "$file" | awk '{print $1}')
    if [[ "$actual" != "$expected" ]]; then
        echo "boot input hash mismatch: $file" >&2
        echo "expected $expected" >&2
        echo "actual   $actual" >&2
        exit 1
    fi
}

check_hash "$fsbl_hash" "$fsbl"
check_hash "$u_boot_hash" "$u_boot"
check_hash "$dtb_hash" "$dtb"
test -s "$bitstream"
if [[ -n "$bitstream_hash" ]]; then
    check_hash "$bitstream_hash" "$bitstream"
fi

mkdir -p "$out_dir"
bif="$out_dir/astra_arty_720p.bif"
cat >"$bif" <<EOF
the_ROM_image:
{
    [bootloader] $fsbl
    $bitstream
    $u_boot
    [load=0x100000] $dtb
}
EOF

"$bootgen" -arch zynq -image "$bif" -o i "$out_dir/BOOT.BIN" -w on
"$bootgen" -arch zynq -read "$out_dir/BOOT.BIN" >"$out_dir/BOOT.BIN.headers.txt"
sha256sum "$fsbl" "$bitstream" "$u_boot" "$dtb" \
    "$out_dir/BOOT.BIN" >"$out_dir/SHA256SUMS"
echo "ASTRA_ARTY_BOOT PASS $out_dir/BOOT.BIN"
