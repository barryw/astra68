#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
rom_output="${TMPDIR:-/tmp}/astrahost-rom-test-$$"
partition_output="${TMPDIR:-/tmp}/astrahost-partition-test-$$"
policy_output="${TMPDIR:-/tmp}/astrahost-block-policy-test-$$"
trap 'rm -f "$rom_output" "$partition_output" "$policy_output"' EXIT

cc -std=c11 -Wall -Wextra -Werror -O2 \
    -I"$script_dir/main" \
    "$script_dir/main/astra_rom.c" \
    "$script_dir/test/test_astra_rom.c" \
    -o "$rom_output"
"$rom_output"

cc -std=c11 -Wall -Wextra -Werror -O2 \
    -I"$script_dir/main" \
    "$script_dir/main/astra_partition.c" \
    "$script_dir/test/test_astra_partition.c" \
    -o "$partition_output"
"$partition_output"

cc -std=c11 -Wall -Wextra -Werror -O2 \
    -I"$script_dir/main" \
    "$script_dir/main/astra_partition.c" \
    "$script_dir/main/astra_block_policy.c" \
    "$script_dir/test/test_astra_block_policy.c" \
    -o "$policy_output"
"$policy_output"
