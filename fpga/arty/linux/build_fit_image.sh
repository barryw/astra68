#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
base_image=${ASTRA_ARTY_BASE_IMAGE:-/home/barry/nova-arty/images/linux/image.ub}
base_image_hash=${ASTRA_ARTY_BASE_IMAGE_SHA256:-32d57541831f765c151dcc5daed35c8a37245cdc2c3541d18604a5e70df3f7a4}
kernel_hash=${ASTRA_ARTY_KERNEL_SHA256:-7049dd117efba0128e48ca609a1984321106ee979a6e7d5deb639681a5521da0}
out_dir=${ASTRA_ARTY_LINUX_OUT:-"$repo_root/build/arty-graphics/linux"}
dtb=${ASTRA_ARTY_DTB:-"$out_dir/astra-system.dtb"}
uboot_tools=${ASTRA_UBOOT_TOOLS:-/home/barry/nova-arty/build/tmp/sysroots-components/x86_64/u-boot-tools-xlnx-native/usr/bin}
dtc_root=${ASTRA_DTC_ROOT:-/home/barry/nova-arty/build/tmp/sysroots-components/x86_64/dtc-native/usr/bin}
dumpimage=${DUMPIMAGE:-"$uboot_tools/dumpimage"}
mkimage=${MKIMAGE:-"$uboot_tools/mkimage"}

: "${SOURCE_DATE_EPOCH:?set SOURCE_DATE_EPOCH for a reproducible FIT image}"

sha256() {
    sha256sum "$1" | awk '{print $1}'
}

test -x "$dumpimage"
test -x "$mkimage"
test -x "$dtc_root/dtc"
test -s "$base_image"
test -s "$dtb"
actual_image_hash=$(sha256 "$base_image")
if [[ "$actual_image_hash" != "$base_image_hash" ]]; then
    echo "base FIT hash mismatch: $base_image" >&2
    echo "expected $base_image_hash" >&2
    echo "actual   $actual_image_hash" >&2
    exit 1
fi

mkdir -p "$out_dir"
work="$out_dir/fit-work"
output="$out_dir/image.ub"
if [[ -e "$work" || -e "$output" ]]; then
    echo "FIT output already exists in $out_dir" >&2
    exit 2
fi
mkdir "$work"

"$dumpimage" -T flat_dt -p 0 -o "$work/kernel.bin" "$base_image"
if [[ $(sha256 "$work/kernel.bin") != "$kernel_hash" ]]; then
    echo "extracted kernel hash mismatch" >&2
    exit 1
fi
cp "$dtb" "$work/astra-system.dtb"

cat >"$work/image.its" <<'EOF'
/dts-v1/;

/ {
    description = "Astra 68 Linux graphics host";
    #address-cells = <1>;

    images {
        kernel-1 {
            description = "Linux kernel";
            data = /incbin/("kernel.bin");
            type = "kernel";
            arch = "arm";
            os = "linux";
            compression = "none";
            load = <0x00200000>;
            entry = <0x00200000>;
            hash-1 { algo = "sha256"; };
        };

        fdt-astra-system.dtb {
            description = "Astra 68 Arty device tree";
            data = /incbin/("astra-system.dtb");
            type = "flat_dt";
            arch = "arm";
            compression = "none";
            hash-1 { algo = "sha256"; };
        };
    };

    configurations {
        default = "conf-astra-system.dtb";
        conf-astra-system.dtb {
            description = "Astra 68 Linux graphics host";
            kernel = "kernel-1";
            fdt = "fdt-astra-system.dtb";
        };
    };
};
EOF

(
    cd "$work"
    PATH="$dtc_root:$PATH" SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" \
        "$mkimage" -f image.its image.ub
)

"$dumpimage" -T flat_dt -p 0 -o "$work/verify-kernel.bin" "$work/image.ub"
"$dumpimage" -T flat_dt -p 1 -o "$work/verify-system.dtb" "$work/image.ub"
cmp "$work/kernel.bin" "$work/verify-kernel.bin"
cmp "$work/astra-system.dtb" "$work/verify-system.dtb"
"$dumpimage" -l "$work/image.ub" >"$out_dir/image.ub.contents.txt"
mv "$work/image.ub" "$output"
sha256sum "$base_image" "$work/kernel.bin" "$dtb" "$output" \
    >"$out_dir/FIT_SHA256SUMS"
echo "ASTRA_ARTY_FIT PASS $output"
