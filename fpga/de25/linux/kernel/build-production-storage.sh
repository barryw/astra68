#!/bin/bash
# Build a complete, distinctly named DE25 kernel package on Beast.
# No board installation or activation occurs here.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
KERNEL_SOURCE=${KERNEL_SOURCE:-$HOME/astra-linux-6.12.11}
KERNEL_OUTPUT=${KERNEL_OUTPUT:-$ROOT/build/de25-kernel-storage/kernel}
PACKAGE=${PACKAGE:-$ROOT/build/de25-kernel-storage/package}
JOBS=${JOBS:-$(nproc)}
SOURCE_COMMIT=d7d192a9ddd955fe206aa350afed8f82e368a74c
RELEASE=6.12.11-astra-storage1-gd7d192a9ddd9
BASELINE=$HERE/de25-6.12.11-gd7d192a9ddd9.config

die() { echo "build-production-storage: $*" >&2; exit 1; }
kmake() {
    make -C "$KERNEL_SOURCE" O="$KERNEL_OUTPUT" ARCH=arm64 \
        CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION= -j"$JOBS" "$@"
}

test "$(git -C "$KERNEL_SOURCE" rev-parse HEAD)" = "$SOURCE_COMMIT" ||
    die "kernel source is not the recorded DE25 source"
test -z "$(git -C "$KERNEL_SOURCE" status --porcelain --untracked-files=no)" ||
    die "kernel source has unrecorded changes"
case "$KERNEL_OUTPUT" in
    "$KERNEL_SOURCE"|"$KERNEL_SOURCE/"*|"$HOME/astra-linux-6.12.11-build")
        die "use a separate output tree; retained vendor build must stay intact" ;;
esac
mkdir -p "$KERNEL_OUTPUT" "$PACKAGE"
source_inputs() {
    sha256sum "$BASELINE" "$HERE/astra-storage.config" \
    "$HERE/astra-production-storage.config" "$0" "$ROOT/fpga/de25/linux/Makefile" \
    "$ROOT/fpga/de25/linux/astra_graphics_arena.c" \
    "$ROOT/fpga/de25/linux/astra_display_capture.c" \
    "$ROOT/fpga/de25/linux/astra_de25_media.h" \
    "$ROOT/fpga/de25/linux/astra_host_arena_uapi.h" \
    "$ROOT/fpga/de25/linux/astra_display_capture_uapi.h" \
    "$ROOT"/sw/include/*.h "$ROOT"/sw/include/astra/*.h
}
inputs=$(source_inputs)
if test -f "$KERNEL_OUTPUT/.config"; then
    test -f "$KERNEL_OUTPUT/astra-inputs.sha256" &&
        test "$inputs" = "$(cat "$KERNEL_OUTPUT/astra-inputs.sha256")" ||
        die "output belongs to different inputs; choose a fresh output tree"
fi
printf '%s\n' "$inputs" > "$KERNEL_OUTPUT/astra-inputs.sha256"
"$KERNEL_SOURCE/scripts/kconfig/merge_config.sh" -m -O "$KERNEL_OUTPUT" \
    "$BASELINE" "$HERE/astra-storage.config" "$HERE/astra-production-storage.config"
kmake olddefconfig
for fragment in "$HERE/astra-storage.config" "$HERE/astra-production-storage.config"; do
    while IFS= read -r line; do
        grep -qxF "$line" "$KERNEL_OUTPUT/.config" ||
            die "olddefconfig dropped $line"
    done < <(grep -E '^(CONFIG_|# CONFIG_.* is not set$)' "$fragment")
done
test "$(kmake -s kernelrelease)" = "$RELEASE" ||
    die "unexpected release; refuse a module set that can shadow the vendor release"
kmake Image modules dtbs
kmake INSTALL_MOD_PATH="$PACKAGE" INSTALL_MOD_STRIP=1 modules_install
make -C "$ROOT/fpga/de25/linux" KERNEL_SOURCE="$KERNEL_SOURCE" \
    KERNEL_OUTPUT="$KERNEL_OUTPUT" MODULE_OUT="$KERNEL_OUTPUT/astra-modules" \
    EXPECTED_KERNEL_RELEASE="$RELEASE" modules
module_dir=$PACKAGE/lib/modules/$RELEASE
mkdir -p "$module_dir/extra/astra" "$PACKAGE/boot" "$PACKAGE/metadata"
cp "$KERNEL_OUTPUT/astra-modules/astra_graphics_arena.ko" \
    "$KERNEL_OUTPUT/astra-modules/astra_display_capture.ko" "$module_dir/extra/astra/"
# Generated module-build links are not installable package content.
for link in build source; do
    test ! -L "$module_dir/$link" || unlink "$module_dir/$link"
done
depmod -b "$PACKAGE" "$RELEASE"
while IFS= read -r -d '' module; do
    magic=$(modinfo -F vermagic "$module")
    test "${magic%% *}" = "$RELEASE" || die "mixed release in $module: $magic"
done < <(find "$module_dir" -type f -name '*.ko' -print0)
test -f "$module_dir/kernel/drivers/scsi/sr_mod.ko" || die "sr_mod missing"
test -f "$module_dir/kernel/drivers/cdrom/cdrom.ko" || die "cdrom missing"
cp "$KERNEL_OUTPUT/arch/arm64/boot/Image" "$PACKAGE/boot/Image"
cp "$KERNEL_OUTPUT/System.map" "$PACKAGE/boot/System.map"
cp "$KERNEL_OUTPUT/.config" "$PACKAGE/metadata/kernel.config"
cp "$KERNEL_OUTPUT/Module.symvers" "$PACKAGE/metadata/Module.symvers"
cp "$KERNEL_OUTPUT/astra-inputs.sha256" "$PACKAGE/metadata/source-inputs.sha256"
printf '%s\n' "$SOURCE_COMMIT" > "$PACKAGE/metadata/kernel-source.commit"
printf '%s\n' "$RELEASE" > "$PACKAGE/metadata/kernel.release"
aarch64-linux-gnu-gcc --version > "$PACKAGE/metadata/compiler.txt"
# Full vendor DTB set belongs to the same build. Board activation selects its
# existing DE25 DTB; this script does not rewrite boot policy.
mkdir -p "$PACKAGE/boot/dtbs"
rsync -a --delete --include '*/' --include '*.dtb' --exclude '*' \
    "$KERNEL_OUTPUT/arch/arm64/boot/dts/" "$PACKAGE/boot/dtbs/"
(
    cd "$PACKAGE"
    find boot lib metadata -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
    sha256sum -c --quiet SHA256SUMS
)
test "$(git -C "$KERNEL_SOURCE" rev-parse HEAD)" = "$SOURCE_COMMIT" &&
    test -z "$(git -C "$KERNEL_SOURCE" status --porcelain --untracked-files=no)" ||
    die "kernel source changed during build"
test "$inputs" = "$(source_inputs)" || die "Astra package inputs changed during build"
echo "Complete kernel package: $PACKAGE ($RELEASE)"
echo "Build verified. Physical boot, graphics, storage, HDMI and optical gates remain."
