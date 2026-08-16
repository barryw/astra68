#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
release_dir=${ASTRA_ARTY_RELEASE_DIR:?set ASTRA_ARTY_RELEASE_DIR}
board=${ASTRA_ARTY_BOARD:-root@192.168.1.188}
ssh=${SSH:-ssh}
scp=${SCP:-scp}
expected_active_boot=${ASTRA_ACTIVE_BOOT_SHA256:-b88b142cc4624ea70dafc65b0aec900d506bcf17f90fc1c7ea6f5f834d8098a5}
expected_active_fit=${ASTRA_ACTIVE_FIT_SHA256:-e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542}

boot="$release_dir/BOOT.BIN"
fit="$release_dir/image.ub"
loader="$release_dir/astra-graphics-boot"
status_tool="$release_dir/astra-boot-status"
sprite_tool="$release_dir/astra-sprite-certify"
render_tool="$release_dir/astra-render-certify"
copper_tool="$release_dir/astra-copper-certify"
audio_tool="$release_dir/astra-audio-certify"
splash="$release_dir/astra_boot_splash.rgb565"
chip_reset="$script_dir/astra_chip_reset.sh"
terminal_start="$script_dir/astra_terminal_start.sh"
firstboot="$script_dir/rootfs-overlay/etc/init.d/astra-firstboot"

for file in "$boot" "$fit" "$loader" "$status_tool" "$sprite_tool" \
    "$render_tool" "$copper_tool" "$audio_tool" "$splash" \
    "$chip_reset" "$terminal_start" "$firstboot"; do
    test -s "$file"
done

sha256() {
    sha256sum "$1" | awk '{print $1}'
}

boot_hash=$(sha256 "$boot")
fit_hash=$(sha256 "$fit")
loader_hash=$(sha256 "$loader")
status_tool_hash=$(sha256 "$status_tool")
sprite_tool_hash=$(sha256 "$sprite_tool")
render_tool_hash=$(sha256 "$render_tool")
copper_tool_hash=$(sha256 "$copper_tool")
audio_tool_hash=$(sha256 "$audio_tool")
splash_hash=$(sha256 "$splash")
chip_reset_hash=$(sha256 "$chip_reset")
terminal_start_hash=$(sha256 "$terminal_start")
release_id=${boot_hash:0:12}
stage="/data/astra/deploy/graphics-$release_id"

"$ssh" "$board" "mkdir -p '$stage'"
"$scp" "$boot" "$board:$stage/BOOT.BIN"
"$scp" "$fit" "$board:$stage/image.ub"
"$scp" "$loader" "$board:$stage/astra-graphics-boot"
"$scp" "$status_tool" "$board:$stage/astra-boot-status"
"$scp" "$sprite_tool" "$board:$stage/astra-sprite-certify"
"$scp" "$render_tool" "$board:$stage/astra-render-certify"
"$scp" "$copper_tool" "$board:$stage/astra-copper-certify"
"$scp" "$audio_tool" "$board:$stage/astra-audio-certify"
"$scp" "$splash" "$board:$stage/astra_boot_splash.rgb565"
"$scp" "$chip_reset" "$board:$stage/astra-chip-reset"
"$scp" "$terminal_start" "$board:$stage/astra-terminal-start"
"$scp" "$firstboot" "$board:$stage/astra-firstboot"

"$ssh" "$board" sh -s -- \
    "$stage" \
    "$expected_active_boot" "$expected_active_fit" \
    "$boot_hash" "$fit_hash" "$loader_hash" "$status_tool_hash" \
    "$sprite_tool_hash" "$render_tool_hash" "$copper_tool_hash" \
    "$audio_tool_hash" "$splash_hash" "$chip_reset_hash" \
    "$terminal_start_hash" <<'REMOTE'
set -eu

stage=$1
expected_active_boot=$2
expected_active_fit=$3
boot_hash=$4
fit_hash=$5
loader_hash=$6
status_tool_hash=$7
sprite_tool_hash=$8
render_tool_hash=$9
copper_tool_hash=${10}
audio_tool_hash=${11}
splash_hash=${12}
chip_reset_hash=${13}
terminal_start_hash=${14}
fat=/run/media/boot-mmcblk0p1
root_writable=0

cleanup() {
    if [ "$root_writable" -eq 1 ]; then
        sync
        mount -o remount,ro / || true
    fi
}
trap cleanup EXIT

check_hash() {
    expected=$1
    file=$2
    actual=$(sha256sum "$file" | awk '{print $1}')
    if [ "$actual" != "$expected" ]; then
        echo "hash mismatch: $file" >&2
        echo "expected $expected" >&2
        echo "actual   $actual" >&2
        exit 1
    fi
}

check_hash "$boot_hash" "$stage/BOOT.BIN"
check_hash "$fit_hash" "$stage/image.ub"
check_hash "$loader_hash" "$stage/astra-graphics-boot"
check_hash "$status_tool_hash" "$stage/astra-boot-status"
check_hash "$sprite_tool_hash" "$stage/astra-sprite-certify"
check_hash "$render_tool_hash" "$stage/astra-render-certify"
check_hash "$copper_tool_hash" "$stage/astra-copper-certify"
check_hash "$audio_tool_hash" "$stage/astra-audio-certify"
check_hash "$splash_hash" "$stage/astra_boot_splash.rgb565"
check_hash "$chip_reset_hash" "$stage/astra-chip-reset"
check_hash "$terminal_start_hash" "$stage/astra-terminal-start"
check_hash "$expected_active_boot" "$fat/BOOT.BIN"
check_hash "$expected_active_fit" "$fat/image.ub"

mkdir -p /data/astra/bin /data/astra/assets /data/astra/log
cp "$stage/astra-graphics-boot" /data/astra/bin/astra-graphics-boot.new
chmod 0755 /data/astra/bin/astra-graphics-boot.new
mv /data/astra/bin/astra-graphics-boot.new \
   /data/astra/bin/astra-graphics-boot
cp "$stage/astra-boot-status" /data/astra/bin/astra-boot-status.new
chmod 0755 /data/astra/bin/astra-boot-status.new
mv /data/astra/bin/astra-boot-status.new \
   /data/astra/bin/astra-boot-status
cp "$stage/astra-sprite-certify" /data/astra/bin/astra-sprite-certify.new
chmod 0755 /data/astra/bin/astra-sprite-certify.new
mv /data/astra/bin/astra-sprite-certify.new \
   /data/astra/bin/astra-sprite-certify
cp "$stage/astra-render-certify" /data/astra/bin/astra-render-certify.new
chmod 0755 /data/astra/bin/astra-render-certify.new
mv /data/astra/bin/astra-render-certify.new \
   /data/astra/bin/astra-render-certify
cp "$stage/astra-copper-certify" /data/astra/bin/astra-copper-certify.new
chmod 0755 /data/astra/bin/astra-copper-certify.new
mv /data/astra/bin/astra-copper-certify.new \
   /data/astra/bin/astra-copper-certify
cp "$stage/astra-audio-certify" /data/astra/bin/astra-audio-certify.new
chmod 0755 /data/astra/bin/astra-audio-certify.new
mv /data/astra/bin/astra-audio-certify.new \
   /data/astra/bin/astra-audio-certify
cp "$stage/astra_boot_splash.rgb565" \
   /data/astra/assets/astra_boot_splash.rgb565.new
mv /data/astra/assets/astra_boot_splash.rgb565.new \
   /data/astra/assets/astra_boot_splash.rgb565
cp "$stage/astra-chip-reset" /data/astra/bin/astra-chip-reset.new
chmod 0755 /data/astra/bin/astra-chip-reset.new
mv /data/astra/bin/astra-chip-reset.new /data/astra/bin/astra-chip-reset
cp "$stage/astra-terminal-start" /data/astra/bin/astra-terminal-start.new
chmod 0755 /data/astra/bin/astra-terminal-start.new
mv /data/astra/bin/astra-terminal-start.new \
   /data/astra/bin/astra-terminal-start
check_hash "$loader_hash" /data/astra/bin/astra-graphics-boot
check_hash "$status_tool_hash" /data/astra/bin/astra-boot-status
check_hash "$sprite_tool_hash" /data/astra/bin/astra-sprite-certify
check_hash "$render_tool_hash" /data/astra/bin/astra-render-certify
check_hash "$copper_tool_hash" /data/astra/bin/astra-copper-certify
check_hash "$audio_tool_hash" /data/astra/bin/astra-audio-certify
check_hash "$splash_hash" /data/astra/assets/astra_boot_splash.rgb565
check_hash "$chip_reset_hash" /data/astra/bin/astra-chip-reset
check_hash "$terminal_start_hash" /data/astra/bin/astra-terminal-start

mount -o remount,rw /
root_writable=1
cp "$stage/astra-firstboot" /etc/init.d/astra-firstboot.new
chmod 0755 /etc/init.d/astra-firstboot.new
mv /etc/init.d/astra-firstboot.new /etc/init.d/astra-firstboot
sync
mount -o remount,ro /
root_writable=0

boot_short=$(printf '%.12s' "$expected_active_boot")
fit_short=$(printf '%.12s' "$expected_active_fit")
rollback_boot="$fat/BOOT.BIN.rollback-$boot_short"
rollback_fit="$fat/image.ub.rollback-$fit_short"
if [ ! -e "$rollback_boot" ]; then
    cp "$fat/BOOT.BIN" "$rollback_boot"
fi
if [ ! -e "$rollback_fit" ]; then
    cp "$fat/image.ub" "$rollback_fit"
fi
check_hash "$expected_active_boot" "$rollback_boot"
check_hash "$expected_active_fit" "$rollback_fit"

cp "$stage/image.ub" "$fat/image.ub.astra-new"
sync
check_hash "$fit_hash" "$fat/image.ub.astra-new"
mv "$fat/image.ub.astra-new" "$fat/image.ub"

cp "$stage/BOOT.BIN" "$fat/BOOT.BIN.astra-new"
sync
check_hash "$boot_hash" "$fat/BOOT.BIN.astra-new"
mv "$fat/BOOT.BIN.astra-new" "$fat/BOOT.BIN"
sync

check_hash "$boot_hash" "$fat/BOOT.BIN"
check_hash "$fit_hash" "$fat/image.ub"
mount | grep -F '/dev/root on / type ext4 (ro,' >/dev/null
echo "ASTRA_ARTY_DEPLOY PASS boot=$boot_hash fit=$fit_hash"
REMOTE

echo "ASTRA_ARTY_DEPLOY STAGED $board release=$release_id"
