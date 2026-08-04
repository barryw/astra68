#!/bin/sh
set -eu

root_device=$(stat -Lc '%d' /)
found=0

for process_dir in /proc/[0-9]*; do
    pid=${process_dir#/proc/}
    command=$(cat "$process_dir/comm" 2>/dev/null || echo unknown)

    for descriptor in "$process_dir"/fd/*; do
        [ -e "$descriptor" ] || continue
        descriptor_device=$(stat -Lc '%d' "$descriptor" 2>/dev/null || true)
        [ "$descriptor_device" = "$root_device" ] || continue

        fd=${descriptor##*/}
        target=$(readlink "$descriptor" 2>/dev/null || echo unknown)
        case "$target" in
            *' (deleted)')
                echo "type=deleted-fd pid=$pid command=$command fd=$fd target=$target"
                found=1
                continue
                ;;
        esac

        flags=$(sed -n 's/^flags:[[:space:]]*//p' \
            "$process_dir/fdinfo/$fd" 2>/dev/null || true)
        [ -n "$flags" ] || continue

        access_mode=$((flags & 3))
        [ "$access_mode" -ne 0 ] || continue

        echo "type=writable-fd pid=$pid command=$command fd=$fd flags=$flags target=$target"
        found=1
    done

    while IFS= read -r mapping; do
        case "$mapping" in
            *' (deleted)')
                echo "type=deleted-map pid=$pid command=$command mapping=$mapping"
                found=1
                ;;
        esac
    done < "$process_dir/maps" 2>/dev/null || true
done

exit "$found"
