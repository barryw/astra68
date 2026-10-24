#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
target=${1:-host}
[[ "$target" == host || "$target" == de25 ]] || { echo 'usage: test_exclusive_fd.sh [host|de25]' >&2; exit 2; }
bash third_party/storage-format/build.sh host
if [[ "$target" == de25 ]]; then bash third_party/storage-format/build.sh de25; fi
cc -std=c11 -O2 -Wall -Wextra -Werror \
    third_party/storage-format/tests/fd_format_probe.c \
    -o build/storage-format/host/fd-format-probe
cc -std=gnu11 -O2 -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes \
    -Ifpga/de25/linux -Iemu/qemu/qemu-9.2/hw/m68k -Indk/include -Isw/include \
    third_party/storage-format/tests/test_format_worker.c \
    fpga/de25/linux/astra_storage_work.c fpga/de25/linux/astra_storage_mount.c \
    fpga/de25/linux/astra_storage_path.c fpga/de25/linux/astra_storage_transport.c \
    emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.c \
    -o build/storage-format/host/test-format-worker -lmount
cc -std=gnu11 -O2 -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes \
    -Ifpga/de25/linux -Iemu/qemu/qemu-9.2/hw/m68k -Indk/include -Isw/include \
    third_party/storage-format/tests/test_partition_worker.c \
    fpga/de25/linux/astra_storage_work.c fpga/de25/linux/astra_storage_mount.c \
    fpga/de25/linux/astra_storage_path.c fpga/de25/linux/astra_storage_transport.c \
    emu/qemu/qemu-9.2/hw/m68k/astra68-resolve.c \
    -o build/storage-format/host/test-partition-worker -lmount
checks="$root/build/storage-format/host"
tools="$root/build/storage-format/$target"
formatter_probe="$checks/fd-format-probe"
if [[ "$target" == host ]]; then formatter_probe="$checks/test-format-worker"; fi
emulator=()
if [[ "$target" == de25 ]]; then
    emulator=("${DE25_SYSROOT:-$HOME/.cache/astra68/de25-jammy-arm64}")
fi

dir=$(mktemp -d /var/tmp/astra-format-test.XXXXXX)
loop=
mounted=0
cleanup() {
    if (( mounted )); then sudo umount "$dir/mount"; fi
    if [[ -n "$loop" ]]; then sudo losetup -d "$loop"; fi
    rm -f "$dir/fat.img" "$dir/exfat.img" "$dir/mbr.img" "$dir/edit.img" "$dir/regular.img"
    rmdir "$dir/mount" "$dir"
}
trap cleanup EXIT
mkdir "$dir/mount"

reread() {
    for attempt in 1 2 3 4 5; do
        sudo udevadm settle
        if sudo blockdev --rereadpt "$loop"; then
            sudo udevadm settle
            return 0
        fi
        sleep 0.2
    done
    return 1
}

truncate -s 128M "$dir/regular.img"
if sudo "$checks/fd-format-probe" fat "$dir/regular.img" "$tools/astra-mkfs.fat" "${emulator[@]}"; then
    echo 'FAT formatter accepted regular file as inherited block device' >&2
    exit 1
fi
if sudo "$checks/fd-format-probe" exfat "$dir/regular.img" "$tools/astra-mkfs.exfat" "${emulator[@]}"; then
    echo 'exFAT formatter accepted regular file as inherited block device' >&2
    exit 1
fi

truncate -s 128M "$dir/fat.img"
loop=$(sudo losetup -f --show --partscan "$dir/fat.img")
if [[ "$target" == host ]]; then
    sudo "$formatter_probe" fat "$loop" "$tools/astra-mkfs.fat" "$(id -u)" "$(id -g)"
else
    sudo "$formatter_probe" fat "$loop" "$tools/astra-mkfs.fat" "${emulator[@]}"
fi
[[ $(sudo blkid -s TYPE -o value "$loop") == vfat ]]
sudo "$checks/astra-fsck.fat" -n "$loop"
sudo mount -t vfat -o ro "$loop" "$dir/mount"
mounted=1
if sudo "$checks/fd-format-probe" fat "$loop" "$tools/astra-mkfs.fat" "${emulator[@]}"; then
    echo 'exclusive descriptor opened while FAT filesystem mounted' >&2
    exit 1
fi
sudo umount "$dir/mount"
mounted=0
sudo losetup -d "$loop"
loop=

truncate -s 128M "$dir/mbr.img"
loop=$(sudo losetup -f --show --partscan "$dir/mbr.img")
printf 'start=2048,size=65536,type=c\n' |
    sudo "$checks/fd-format-probe" mbr "$loop" /usr/sbin/sfdisk
sudo udevadm settle
if [[ ! -b "${loop}p1" ]]; then
    for attempt in 1 2 3 4 5; do
        if sudo blockdev --rereadpt "$loop"; then break; fi
        sleep 0.2
    done
fi
[[ -b "${loop}p1" ]]
sudo losetup -d "$loop"
loop=

truncate -s 128M "$dir/edit.img"
loop=$(sudo losetup -f --show --partscan "$dir/edit.img")
for label in dos gpt; do
    map=mbr
    if [[ "$label" == gpt ]]; then map=gpt; fi
    sudo "$checks/test-partition-worker" init "$map" "$loop" /usr/sbin/sfdisk
    sudo sfdisk --dump "$loop" | grep -q "label: $label"
    sudo "$checks/test-partition-worker" create "$map" "$loop" /usr/sbin/sfdisk
    sudo sfdisk --dump "$loop" | grep -Eq 'start= *2048'
    reread
    [[ -b "${loop}p1" ]]
    sudo "$checks/test-partition-worker" delete "$map" "$loop" /usr/sbin/sfdisk
    reread
    [[ ! -b "${loop}p1" ]]
done
sudo losetup -d "$loop"
loop=

truncate -s 128M "$dir/exfat.img"
loop=$(sudo losetup -f --show --partscan "$dir/exfat.img")
if [[ "$target" == host ]]; then
    sudo "$formatter_probe" exfat "$loop" "$tools/astra-mkfs.exfat" "$(id -u)" "$(id -g)"
else
    sudo "$formatter_probe" exfat "$loop" "$tools/astra-mkfs.exfat" "${emulator[@]}"
fi
[[ $(sudo blkid -s TYPE -o value "$loop") == exfat ]]
sudo "$checks/astra-fsck.exfat" -n "$loop"
sudo losetup -d "$loop"
loop=
echo "$target exclusive FAT32/exFAT format and MBR/GPT edit: PASS"
