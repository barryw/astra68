#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
out_dir=${ASTRA_ARTY_OUT:-"$repo_root/build/arty-graphics-v1"}
vivado=${VIVADO:-vivado}

python3 "$repo_root/fpga/arty/graphics/protocol/generate_protocol.py"
python3 "$repo_root/fpga/arty/graphics/test_hdmi_source_contract.py"
mkdir -p "$out_dir"
exec "$vivado" -mode batch \
    -log "$out_dir/vivado.log" \
    -journal "$out_dir/vivado.jou" \
    -source "$repo_root/fpga/arty/scripts/build_graphics.tcl"
