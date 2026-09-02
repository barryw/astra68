#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
repository=$(cd "$script_dir/../../.." && pwd)
release_dir=${ASTRA_ARTY_RELEASE_DIR:?set ASTRA_ARTY_RELEASE_DIR}
board=${ASTRA_ARTY_BOARD:-root@192.168.1.188}
ssh=${SSH:-ssh}
scp=${SCP:-scp}
expected_active_boot=${ASTRA_ACTIVE_BOOT_SHA256:?set ASTRA_ACTIVE_BOOT_SHA256}
expected_active_fit=${ASTRA_ACTIVE_FIT_SHA256:?set ASTRA_ACTIVE_FIT_SHA256}
expected_splash=${ASTRA_SPLASH_SHA256:-86eb30739db77b85f4deb1915fb9cb9263ab4755ae318ffb1b7a4a95b7017ba4}

boot="$release_dir/BOOT.BIN"
fit="$release_dir/image.ub"
loader="$release_dir/astra-graphics-boot"
status_tool="$release_dir/astra-boot-status"
sprite_tool="$release_dir/astra-sprite-certify"
render_tool="$release_dir/astra-render-certify"
copper_tool="$release_dir/astra-copper-certify"
audio_tool="$release_dir/astra-audio-certify"
hdmi_link="$release_dir/astra-hdmi-link"
splash="$release_dir/astra_boot_splash.rgb565"
terminal_display="$release_dir/astra-terminal-display"
time_sync="$release_dir/astra-time-sync"
chip_reset="$script_dir/astra_chip_reset.sh"
terminal_start="$script_dir/astra_terminal_start.sh"
firstboot="$script_dir/rootfs-overlay/etc/init.d/astra-firstboot"
resolv="$script_dir/rootfs-overlay/etc/resolv.conf"
readonly_root="$script_dir/rootfs-overlay/usr/sbin/astra-configure-readonly-root"
release_tool="$repository/tools/astra_release.py"

for file in "$boot" "$fit" "$loader" "$status_tool" "$sprite_tool" \
    "$render_tool" "$copper_tool" "$audio_tool" "$hdmi_link" "$splash" \
    "$terminal_display" "$time_sync" "$chip_reset" "$terminal_start" \
    "$firstboot" "$resolv" "$readonly_root" "$release_tool"; do
    test -s "$file"
done

sha256() {
    sha256sum "$1" | awk '{print $1}'
}

splash_hash=$(sha256 "$splash")
if [ "$splash_hash" != "$expected_splash" ]; then
    echo "refusing nonblank or unqualified boot splash: $splash" >&2
    echo "expected $expected_splash" >&2
    echo "actual   $splash_hash" >&2
    exit 1
fi

work=$(mktemp -d)
remote_incoming=
cleanup_local() {
    status=$?
    rm -rf "$work"
    if [ -n "$remote_incoming" ]; then
        "$ssh" "$board" "rm -rf '$remote_incoming'" >/dev/null 2>&1 || true
    fi
    exit "$status"
}
trap cleanup_local EXIT
local_release="$work/release"
release_id=$(PYTHONDONTWRITEBYTECODE=1 python3 "$release_tool" create \
    "$local_release" \
    "BOOT.BIN=$boot" \
    "image.ub=$fit" \
    "bin/astra-graphics-boot=$loader" \
    "bin/astra-boot-status=$status_tool" \
    "bin/astra-sprite-certify=$sprite_tool" \
    "bin/astra-render-certify=$render_tool" \
    "bin/astra-copper-certify=$copper_tool" \
    "bin/astra-audio-certify=$audio_tool" \
    "bin/astra-hdmi-link=$hdmi_link" \
    "bin/astra-terminal-display=$terminal_display" \
    "bin/astra-time-sync=$time_sync" \
    "bin/astra-chip-reset=$chip_reset" \
    "bin/astra-terminal-start=$terminal_start" \
    "bin/astra-release.py=$release_tool" \
    "assets/astra_boot_splash.rgb565=$splash" \
    "etc/init.d/astra-firstboot=$firstboot" \
    "etc/resolv.conf=$resolv" \
    "sbin/astra-configure-readonly-root=$readonly_root")
remote_incoming="/data/astra/graphics/incoming-$release_id-$$"

"$ssh" "$board" "mkdir -p '$remote_incoming'"
"$scp" -r "$local_release/." "$board:$remote_incoming/"

"$ssh" "$board" sh -s -- \
    "$remote_incoming" "$release_id" \
    "$expected_active_boot" "$expected_active_fit" <<'REMOTE'
set -eu

incoming=$1
release_id=$2
expected_active_boot=$3
expected_active_fit=$4
store=/data/astra/graphics
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

actual_id=$(PYTHONDONTWRITEBYTECODE=1 \
    python3 "$incoming/bin/astra-release.py" verify "$incoming")
if [ "$actual_id" != "$release_id" ]; then
    echo "transferred graphics release identity changed" >&2
    exit 1
fi
installed_id=$(PYTHONDONTWRITEBYTECODE=1 \
    python3 "$incoming/bin/astra-release.py" install --no-activate \
        "$incoming" "$store")
if [ "$installed_id" != "$release_id" ]; then
    echo "installed graphics release identity changed" >&2
    exit 1
fi
[ ! -e "$incoming" ] || rm -rf "$incoming"
stage=$store/releases/$release_id
boot_hash=$(sha256sum "$stage/BOOT.BIN" | awk '{print $1}')
mkdir -p "$store/by-boot"
selected_id=$(PYTHONDONTWRITEBYTECODE=1 \
    python3 "$stage/bin/astra-release.py" select "$store" \
        "by-boot/$boot_hash" "$release_id")
if [ "$selected_id" != "$release_id" ]; then
    echo "graphics boot selector identity changed" >&2
    exit 1
fi
check_hash "$expected_active_boot" "$fat/BOOT.BIN"
check_hash "$expected_active_fit" "$fat/image.ub"

mount -o remount,rw /
root_writable=1
cp "$stage/sbin/astra-configure-readonly-root" \
    /usr/sbin/astra-configure-readonly-root.new
chmod 0755 /usr/sbin/astra-configure-readonly-root.new
mv /usr/sbin/astra-configure-readonly-root.new \
    /usr/sbin/astra-configure-readonly-root
/usr/sbin/astra-configure-readonly-root
cp "$stage/etc/init.d/astra-firstboot" /etc/init.d/astra-firstboot.new
chmod 0755 /etc/init.d/astra-firstboot.new
mv /etc/init.d/astra-firstboot.new /etc/init.d/astra-firstboot
cp "$stage/etc/resolv.conf" /var/run/resolv.conf
check_hash "$(sha256sum "$stage/etc/resolv.conf" | awk '{print $1}')" \
    /etc/resolv.conf
ln -sf ../init.d/astra-firstboot /etc/rc5.d/S02astra-firstboot
rm -f /etc/rcS.d/S03astra-firstboot /etc/rcS.d/S04astra-firstboot
sync
mount -o remount,ro /
root_writable=0

fit_hash=$(sha256sum "$stage/image.ub" | awk '{print $1}')
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
echo "ASTRA_ARTY_DEPLOY PASS release=$release_id boot=$boot_hash fit=$fit_hash"
REMOTE
remote_incoming=

echo "ASTRA_ARTY_DEPLOY STAGED $board release=$release_id"
