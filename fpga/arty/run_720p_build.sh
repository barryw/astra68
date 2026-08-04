#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
out_dir=${ASTRA_ARTY_OUT:-"$repo_root/build/arty-720p"}
vivado=${VIVADO:-vivado}

mkdir -p "$out_dir"
exec "$vivado" -mode batch \
    -log "$out_dir/vivado.log" \
    -journal "$out_dir/vivado.jou" \
    -source "$repo_root/fpga/arty/scripts/build_720p.tcl"
