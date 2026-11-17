#!/bin/sh
# Exercise V1, V2 and V7 execution with redistributable interactive stories.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/astraz-legacy.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM

python3 "$root/emu/qemu/astraz-media/fixture.py" "$temporary"
sh "$root/emu/qemu/astraz-media/build-host-trace.sh" "$temporary/build"
for version in 1 2 7; do
    "$temporary/build/astraz-host-trace" \
        "$temporary/legacy.z$version" - 2 > "$temporary/z$version.trace"
    grep -q "screen kind=1 version=$version " "$temporary/z$version.trace"
    grep -q 'Room. Press Return to open door:' "$temporary/z$version.trace"
    grep -q 'INPUT line max=' "$temporary/z$version.trace"
    grep -q 'The door opens.' "$temporary/z$version.trace"
    grep -q 'RESULT status=0 .*reads=1 .*picture_oob=0 cursor_oob=0' \
        "$temporary/z$version.trace"
    if [ "$version" -le 2 ]; then
        grep -q 'char window=7 U+0072' "$temporary/z$version.trace"
    else
        grep -q 'V7 routine reached.' "$temporary/z$version.trace"
    fi
done
printf 'AstraZ Z1/Z2/Z7 interactive host gates: PASS\n'
