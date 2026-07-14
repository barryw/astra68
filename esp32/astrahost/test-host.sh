#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
output="${TMPDIR:-/tmp}/astrahost-rom-test-$$"
trap 'rm -f "$output"' EXIT

cc -std=c11 -Wall -Wextra -Werror -O2 \
    -I"$script_dir/main" \
    "$script_dir/main/astra_rom.c" \
    "$script_dir/test/test_astra_rom.c" \
    -o "$output"
"$output"
