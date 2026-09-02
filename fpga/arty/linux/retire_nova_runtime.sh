#!/bin/sh
set -eu

OVERLAY_ROOT=${1:-/data/astra/rootfs-overlay}
ASTRA_FIRSTBOOT="$OVERLAY_ROOT/etc/init.d/astra-firstboot"
ASTRA_HOSTNAME="$OVERLAY_ROOT/etc/hostname"
ASTRA_HOSTS="$OVERLAY_ROOT/etc/hosts"
ASTRA_RESOLV="$OVERLAY_ROOT/etc/resolv.conf"
ASTRA_SAMBA="$OVERLAY_ROOT/etc/samba/smb.conf"
ASTRA_MODULE_BLACKLIST="$OVERLAY_ROOT/etc/modprobe.d/astra-blacklist.conf"
ASTRA_READONLY_ROOT="$OVERLAY_ROOT/usr/sbin/astra-configure-readonly-root"

for required_file in \
    "$ASTRA_FIRSTBOOT" \
    "$ASTRA_HOSTNAME" \
    "$ASTRA_HOSTS" \
    "$ASTRA_RESOLV" \
    "$ASTRA_SAMBA" \
    "$ASTRA_MODULE_BLACKLIST" \
    "$ASTRA_READONLY_ROOT"; do
    if [ ! -f "$required_file" ]; then
        echo "missing staged Astra rootfs file: $required_file" >&2
        exit 2
    fi
done

backup_dir=/data/astra/retired-nova
backup_stamp=$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$backup_dir" /data/astra/share
tar -cf "$backup_dir/rootfs-$backup_stamp.tar" \
    /etc/hostname \
    /etc/hosts \
    /etc/samba/smb.conf \
    /etc/init.d/nova-firstboot \
    /etc/init.d/novavm \
    /etc/rcS.d/S03nova-firstboot \
    /etc/rcS.d/S04novavm \
    /etc/rcS.d/S03astra-firstboot \
    /etc/modules-load.d/novacap.conf \
    /usr/bin/novavm 2>/dev/null || true

# novacap is also discoverable through its device-tree modalias. Removing only
# modules-load.d does not stop udev from probing it against the retired Nova PL
# address range, where the first MMIO access stalls the AXI bus.
novacap_modules=$(find /lib/modules -type f -name 'novacap.ko*' 2>/dev/null || true)
for module_path in $novacap_modules; do
    module_relative=${module_path#/}
    module_backup_dir="$backup_dir/modules/$(dirname "$module_relative")"
    mkdir -p "$module_backup_dir"
    cp -p "$module_path" "$module_backup_dir/"
done

# The deployed appliance intentionally keeps / read-only. Restrict the writable
# interval to replacing init ownership, then restore it even on failure.
mount -o remount,rw /
restore_read_only() {
    status=$?
    trap - EXIT INT TERM
    sync
    if ! mount -o remount,ro /; then
        echo "failed to restore read-only root filesystem" >&2
        exit 1
    fi
    exit "$status"
}
trap restore_read_only EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cp "$ASTRA_FIRSTBOOT" /etc/init.d/astra-firstboot
chmod 0755 /etc/init.d/astra-firstboot
cp "$ASTRA_HOSTNAME" /etc/hostname
cp "$ASTRA_HOSTS" /etc/hosts
cp "$ASTRA_SAMBA" /etc/samba/smb.conf
mkdir -p /etc/modprobe.d
cp "$ASTRA_MODULE_BLACKLIST" /etc/modprobe.d/astra-blacklist.conf
mkdir -p /usr/sbin
cp "$ASTRA_READONLY_ROOT" /usr/sbin/astra-configure-readonly-root
chmod 0755 /usr/sbin/astra-configure-readonly-root
/usr/sbin/astra-configure-readonly-root
cp "$ASTRA_RESOLV" /var/run/resolv.conf
ln -sf ../init.d/astra-firstboot /etc/rc5.d/S02astra-firstboot
rm -f \
    /etc/rcS.d/S03astra-firstboot \
    /etc/rcS.d/S04astra-firstboot \
    /etc/rcS.d/S03nova-firstboot \
    /etc/rcS.d/S04novavm
rm -f /etc/init.d/nova-firstboot /etc/init.d/novavm /usr/bin/novavm
rm -f /etc/modules-load.d/novacap.conf
for module_path in $novacap_modules; do
    rm -f "$module_path"
done
if command -v depmod >/dev/null 2>&1; then
    depmod -a
    if [ -x /etc/init.d/udev ] && pidof udevd >/dev/null 2>&1; then
        # depmod atomically replaces module indexes. A running udevd retains
        # mappings to the unlinked files, which makes a read-only remount fail
        # with EBUSY until udevd releases them.
        /etc/init.d/udev restart
    fi
fi
hostname astra-arty

if [ -x /etc/init.d/samba ]; then
    if command -v killall >/dev/null 2>&1; then
        killall nmbd 2>/dev/null || true
        killall smbd 2>/dev/null || true
        sleep 1
    else
        /etc/init.d/samba stop || true
    fi
    /etc/init.d/samba start || true
fi

echo "Nova userspace retired; Astra first-boot ownership installed."
echo "Backup: $backup_dir/rootfs-$backup_stamp.tar"
