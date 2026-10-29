#!/bin/sh
# Check standard lower-window Z-machine style events with a libre story.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/astraz-style.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM

python3 "$root/emu/qemu/astraz-media/fixture.py" "$temporary"
sh "$root/emu/qemu/astraz-media/build-host-trace.sh" "$temporary/build"
"$temporary/build/astraz-host-trace" "$temporary/style-display.z5" \
    - 2 > "$temporary/trace"

for style in 0 1 2 3 4; do
    grep -q "style window=0 flags=$style" "$temporary/trace"
done
grep -q 'colour window=0 fg=3 bg=9' "$temporary/trace"
grep -q 'INPUT key timeout=0 cursor=1' "$temporary/trace"
grep -q 'RESULT status=0 .*reads=1 .*bold=2 emphasis=1 reverse=2 ' \
    "$temporary/trace"
printf 'AstraZ Z5 lower-window styles: PASS\n'
