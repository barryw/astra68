#!/usr/bin/env bash
set -euo pipefail

worker=${1:?pass AArch64 test-partition-worker path}
[[ -x "$worker" ]] || { echo 'missing partition worker' >&2; exit 2; }
[[ $(id -u) == 0 ]] || { echo 'run as root on DE25' >&2; exit 2; }

dir=$(mktemp -d /dev/shm/astra-partition-test.XXXXXX)
image="$dir/disk.img"
loop=
cleanup() {
    if [[ -n "$loop" ]]; then losetup -d "$loop"; fi
    rm -f "$image"
    rmdir "$dir"
}
trap cleanup EXIT
truncate -s 48M "$image"
loop=$(losetup -f --show --partscan "$image")
[[ "$loop" != /dev/loop0 ]]
[[ $(losetup -j "$image" | wc -l) == 1 ]]

reread() {
    for attempt in 1 2 3 4 5; do
        udevadm settle
        if blockdev --rereadpt "$loop"; then
            udevadm settle
            return 0
        fi
        sleep 0.2
    done
    return 1
}

for map in mbr gpt; do
    "$worker" init "$map" "$loop" /usr/sbin/sfdisk
    label=dos
    if [[ "$map" == gpt ]]; then label=gpt; fi
    sfdisk --dump "$loop" | grep -q "label: $label"
    "$worker" create "$map" "$loop" /usr/sbin/sfdisk
    sfdisk --dump "$loop" | grep -Eq 'start= *2048'
    reread
    [[ -b "${loop}p1" ]]
    "$worker" delete "$map" "$loop" /usr/sbin/sfdisk
    reread
    [[ ! -b "${loop}p1" ]]
done
echo 'DE25 exclusive MBR/GPT partition worker: PASS'
