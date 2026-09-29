#!/usr/bin/env bash
# Render engine cycle accounting under the DDR-like latency model.
# usage: [PERF_ARGS=+blit_rows=N] run_perf.sh [READ_LAT] [WRITE_LAT] [CASES-hex]
#   cases: 1 fill 1x1, 2 fill 8x8, 4 line, 8 fill 640x480, 10 copy 640x480,
#          20 testdraw2 frame, 40 TRIANGLES quads vs FILL, 80 quad coverage,
#          200 FILL_RECTS, 400 testdraw2 frame with FILL_RECTS batching,
#          800 testdraw2 frame with FILL_RECTS and LINES, LINES segments,
#          1000 blended FILL_RECTS, 2000 ARGB8888->RGB565 BLIT (NONE, BLEND),
#          4000 scaled ARGB8888->RGB565 BLIT (408x167 -> 640x480)
#   PERF_ARGS=+host places the batch (ring, descriptors, sources) in the
#   host aperture, read at HOST_LAT (default 60) cycles.
set -euo pipefail
ROOT=${ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}
G=$ROOT/fpga/arty/graphics
B=${BUILD:-$ROOT/build/arty-graphics/perf}; mkdir -p "$B"
RL=${1:-25}; WL=${2:-$RL}; C=${3:-3f}
iverilog -g2012 -I "$G" -Ptb_astra_render_perf.READ_LATENCY=$RL \
    -Ptb_astra_render_perf.HOST_READ_LATENCY=${HOST_LAT:-60} \
    -Ptb_astra_render_perf.WRITE_LATENCY=$WL \
    -s tb_astra_render_perf -o "$B/perf_${RL}_${WL}_${HOST_LAT:-60}" \
    "$G"/astra_render_{surface_validator,pixel_writer,copy_burst,blitter,geometry,flood,glyph,texture,command_processor,host_reads}.sv \
    "$G/sim/astra_render_axi_latency_model.sv" "$G/sim/tb_astra_render_perf.sv"
cd "$ROOT" && vvp -n "$B/perf_${RL}_${WL}_${HOST_LAT:-60}" +cases=$C ${PERF_ARGS:-}
