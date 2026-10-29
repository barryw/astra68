#!/bin/sh
# Run on Beast with privately held Z8 One Room and Z4 Trinity story files.
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 ONEROOM.Z8 TRINITY.Z4 OUTPUT_DIRECTORY" >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
output=$3
mkdir -p "$output"
sh "$root/emu/qemu/astraz-media/build-host-trace.sh" "$output"
trace="$output/astraz-host-trace"

ASTRAZ_TRACE_INPUT=look "$trace" "$1" - 3 > "$output/oneroom-look.log"
ASTRAZ_TRACE_INPUT=look "$trace" "$2" - 3 > "$output/trinity-look.log"

grep -q 'bathroom_headings=2 bathroom_physical_B=2' \
    "$output/oneroom-look.log"
grep -q 'RESULT status=7' "$output/oneroom-look.log"
grep -q 'RESULT status=7' "$output/trinity-look.log"

# A paragraph longer than the 80-column Frotz screen must remain one
# semantic line. Blank lines between paragraphs are explicit story output.
for log in "$output/oneroom-look.log" "$output/trinity-look.log"; do
    awk '
        /SEMANTIC_TRANSCRIPT_BEGIN/ { in_transcript = 1; next }
        /SEMANTIC_TRANSCRIPT_END/ { in_transcript = 0 }
        in_transcript && length($0) > 120 { long_line = 1 }
        in_transcript && $0 == "" { blank_line = 1 }
        END { exit !(long_line && blank_line) }
    ' "$log"
done

echo 'AstraZ semantic Z8/Z4 real-story trace: PASS'
