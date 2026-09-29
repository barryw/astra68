#!/usr/bin/env bash
# Memory-ordering bench: slow writes visible only at their response.
set -euo pipefail
ROOT=${ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}
G=$ROOT/fpga/arty/graphics
B=${BUILD:-$ROOT/build/arty-graphics}; mkdir -p "$B"
iverilog -g2012 -I "$G" -s tb_astra_render_ordering -o "$B/tb_astra_render_ordering" \
    "$G"/astra_render_{surface_validator,pixel_writer,copy_burst,blitter,geometry,flood,glyph,texture,command_processor}.sv \
    "$G/sim/astra_render_axi_latency_model.sv" "$G/sim/tb_astra_render_ordering.sv"
cd "$ROOT" && vvp -n "$B/tb_astra_render_ordering"
