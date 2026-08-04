#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
out_dir=${ASTRA_SIM_OUT:-"$repo_root/build/arty-720p-sim"}

mkdir -p "$out_dir"
iverilog -g2012 \
    -s tb_astra_720p_pattern \
    -o "$out_dir/tb_astra_720p_pattern" \
    "$repo_root/fpga/arty/rtl/astra_720p_pattern.sv" \
    "$repo_root/fpga/arty/sim/tb_astra_720p_pattern.sv"
vvp "$out_dir/tb_astra_720p_pattern"
