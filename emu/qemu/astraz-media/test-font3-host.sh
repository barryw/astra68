#!/bin/sh
# Exercise the private engine's graphics-font contract with a libre Z6 story.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/astraz-font3.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM

python3 "$root/emu/qemu/astraz-media/fixture.py" "$temporary"
sh "$root/emu/qemu/astraz-media/build-host-trace.sh" "$temporary/build"
"$temporary/build/astraz-host-trace" "$temporary/font3.z6" > "$temporary/trace"

grep -q 'RESULT status=0 .*characters=2 .*font3=1 ' "$temporary/trace"
awk '
    /font=3/ { if (state == 0) state = 1; next }
    /char window=0 U\+0041/ { if (state == 1) state = 2; next }
    /font=1/ { if (state == 2) state = 3; next }
    /char window=0 U\+0042/ { if (state == 3) state = 4 }
    END { exit state == 4 ? 0 : 1 }
' "$temporary/trace"
"$temporary/build/astraz-host-trace" "$temporary/font3-display.z6" \
    "$temporary/media.blb" 2 > "$temporary/display-trace"
grep -q 'INPUT key timeout=0 cursor=1' "$temporary/display-trace"
grep -q 'RESULT status=0 .*characters=2 reads=1 .*font3=1 ' \
    "$temporary/display-trace"
printf 'AstraZ Z6 graphics font 3 event order: PASS\n'
