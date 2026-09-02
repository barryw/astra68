#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BUILD=${BUILD:-"$ROOT/build/arty-graphics"}

mkdir -p "$BUILD"
python3 "$ROOT/fpga/arty/graphics/protocol/generate_protocol.py"
python3 "$ROOT/fpga/arty/graphics/test_hdmi_source_contract.py"

iverilog -g2012 -Wall \
    -s tb_hdmi_source_mode \
    -o "$BUILD/tb_hdmi_source_mode" \
    "$ROOT/third_party/hdl-util-hdmi/hdmi_mode_control.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_hdmi_source_mode.sv"

vvp "$BUILD/tb_hdmi_source_mode"

iverilog -g2012 -Wall \
    -s tb_astra_axi_read_3to1 \
    -o "$BUILD/tb_astra_axi_read_3to1" \
    "$ROOT/fpga/arty/graphics/astra_axi_read_3to1.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_axi_read_3to1.sv"

vvp "$BUILD/tb_astra_axi_read_3to1"

iverilog -g2012 -Wall \
    -s tb_astra_front_panel_axi \
    -o "$BUILD/tb_astra_front_panel_axi" \
    "$ROOT/fpga/arty/common/astra_front_panel.sv" \
    "$ROOT/fpga/arty/rtl/astra_front_panel_axi.sv" \
    "$ROOT/fpga/arty/rtl/sim/tb_astra_front_panel_axi.sv"

vvp "$BUILD/tb_astra_front_panel_axi"

iverilog -g2012 -Wall \
    -s tb_astra_boot_text_overlay \
    -o "$BUILD/tb_astra_boot_text_overlay" \
    "$ROOT/fpga/arty/graphics/astra_boot_text_overlay.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_boot_text_overlay.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_boot_text_overlay")

iverilog -g2012 -Wall \
    -s tb_astra_tile_span_walker \
    -o "$BUILD/tb_astra_tile_span_walker" \
    "$ROOT/fpga/arty/graphics/astra_tile_span_walker.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_tile_span_walker.sv"

vvp "$BUILD/tb_astra_tile_span_walker"

iverilog -g2012 -Wall \
    -s tb_astra_tile_line_builder \
    -o "$BUILD/tb_astra_tile_line_builder" \
    "$ROOT/fpga/arty/graphics/astra_tile_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_tile_span_walker.sv" \
    "$ROOT/fpga/arty/graphics/astra_tile_line_store.sv" \
    "$ROOT/fpga/arty/graphics/astra_tile_line_builder.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_tile_line_builder.sv"

vvp "$BUILD/tb_astra_tile_line_builder"

iverilog -g2012 -Wall \
    -s tb_astra_tile_line_builder_perf \
    -o "$BUILD/tb_astra_tile_line_builder_perf" \
    "$ROOT/fpga/arty/graphics/astra_tile_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_tile_span_walker.sv" \
    "$ROOT/fpga/arty/graphics/astra_tile_line_store.sv" \
    "$ROOT/fpga/arty/graphics/astra_tile_line_builder.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_tile_line_builder_perf.sv"

vvp "$BUILD/tb_astra_tile_line_builder_perf"

iverilog -g2012 -Wall \
    -s tb_astra_framebuffer_line_builder \
    -o "$BUILD/tb_astra_framebuffer_line_builder" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_store.sv" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_builder.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_framebuffer_line_builder.sv"

vvp "$BUILD/tb_astra_framebuffer_line_builder"

iverilog -g2012 -Wall \
    -s tb_astra_sprite_scene_store \
    -o "$BUILD/tb_astra_sprite_scene_store" \
    "$ROOT/fpga/arty/graphics/astra_sprite_scene_store.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_sprite_scene_store.sv"

vvp "$BUILD/tb_astra_sprite_scene_store"

iverilog -g2012 -Wall \
    -s tb_astra_premult_blend \
    -o "$BUILD/tb_astra_premult_blend" \
    "$ROOT/fpga/arty/graphics/astra_premult_blend.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_premult_blend.sv"

vvp "$BUILD/tb_astra_premult_blend"

SPRITE_SOURCES=(
    "$ROOT/fpga/arty/graphics/astra_sprite_scene_store.sv"
    "$ROOT/fpga/arty/graphics/astra_premult_blend.sv"
    "$ROOT/fpga/arty/graphics/astra_sprite_line_store.sv"
    "$ROOT/fpga/arty/graphics/astra_sprite_line_builder.sv"
    "$ROOT/fpga/arty/graphics/sim/tb_astra_sprite_line_builder.sv"
)

run_sprite_line_builder() {
    local mode=$1
    local name=$2
    iverilog -g2012 -Wall \
        -Ptb_astra_sprite_line_builder.PERF_MODE="$mode" \
        -s tb_astra_sprite_line_builder \
        -o "$BUILD/tb_astra_sprite_line_builder_$name" \
        "${SPRITE_SOURCES[@]}"
    vvp "$BUILD/tb_astra_sprite_line_builder_$name"
}

run_sprite_line_builder 0 functional
run_sprite_line_builder 1 worst_case
run_sprite_line_builder 2 overflow
run_sprite_line_builder 3 slverr
run_sprite_line_builder 4 deadline
run_sprite_line_builder 5 collision16
run_sprite_line_builder 6 split4k
run_sprite_line_builder 7 variable_dimensions
run_sprite_line_builder 8 count_limit

iverilog -g2012 -Wall \
    -s tb_astra_pixel_compositor \
    -o "$BUILD/tb_astra_pixel_compositor" \
    "$ROOT/fpga/arty/common/astra_async_fifo.sv" \
    "$ROOT/fpga/arty/graphics/astra_palette_store.sv" \
    "$ROOT/fpga/arty/graphics/astra_premult_blend.sv" \
    "$ROOT/fpga/arty/graphics/astra_pixel_compositor.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_pixel_compositor.sv"

vvp "$BUILD/tb_astra_pixel_compositor"

iverilog -g2012 -Wall \
    -s tb_astra_palette_store \
    -o "$BUILD/tb_astra_palette_store" \
    "$ROOT/fpga/arty/common/astra_async_fifo.sv" \
    "$ROOT/fpga/arty/graphics/astra_palette_store.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_palette_store.sv"

vvp "$BUILD/tb_astra_palette_store"

iverilog -g2012 -Wall \
    -s tb_astra_line_scheduler \
    -o "$BUILD/tb_astra_line_scheduler" \
    "$ROOT/fpga/arty/graphics/astra_line_scheduler.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_line_scheduler.sv"

vvp "$BUILD/tb_astra_line_scheduler"

iverilog -g2012 -Wall \
    -s tb_astra_axi_lite_1to2 \
    -o "$BUILD/tb_astra_axi_lite_1to2" \
    "$ROOT/fpga/arty/graphics/astra_axi_lite_1to2.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_axi_lite_1to2.sv"

vvp "$BUILD/tb_astra_axi_lite_1to2"

iverilog -g2012 -Wall \
    -s tb_astra_copper \
    -o "$BUILD/tb_astra_copper" \
    "$ROOT/fpga/arty/graphics/astra_copper.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_copper.sv"

vvp "$BUILD/tb_astra_copper"

iverilog -g2012 -Wall \
    -s tb_astra_copper_control \
    -o "$BUILD/tb_astra_copper_control" \
    "$ROOT/fpga/arty/graphics/astra_copper.sv" \
    "$ROOT/fpga/arty/graphics/astra_copper_control.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_copper_control.sv"

vvp "$BUILD/tb_astra_copper_control"

iverilog -g2012 -Wall \
    -s tb_astra_copper_beam_scheduler \
    -o "$BUILD/tb_astra_copper_beam_scheduler" \
    "$ROOT/fpga/arty/graphics/astra_copper_beam_scheduler.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_copper_beam_scheduler.sv"

vvp "$BUILD/tb_astra_copper_beam_scheduler"

iverilog -g2012 -Wall \
    -s tb_astra_copper_registers \
    -o "$BUILD/tb_astra_copper_registers" \
    "$ROOT/fpga/arty/graphics/astra_copper_registers.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_copper_registers.sv"

vvp "$BUILD/tb_astra_copper_registers"

iverilog -g2012 -Wall \
    -s tb_astra_copper_structural_state \
    -o "$BUILD/tb_astra_copper_structural_state" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_tile_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_copper_structural_state.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_copper_structural_state.sv"

vvp "$BUILD/tb_astra_copper_structural_state"

iverilog -g2012 -Wall \
    -s tb_astra_copper_pixel_events \
    -o "$BUILD/tb_astra_copper_pixel_events" \
    "$ROOT/fpga/arty/common/astra_async_fifo.sv" \
    "$ROOT/fpga/arty/graphics/astra_copper_pixel_events.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_copper_pixel_events.sv"

vvp "$BUILD/tb_astra_copper_pixel_events"

iverilog -g2012 -Wall \
    -s tb_astra_graphics_control \
    -o "$BUILD/tb_astra_graphics_control" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_tile_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_sprite_scene_store.sv" \
    "$ROOT/fpga/arty/graphics/astra_graphics_control.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_graphics_control.sv"

vvp "$BUILD/tb_astra_graphics_control"

PIPELINE_SOURCES=(
    "$ROOT/fpga/arty/graphics/astra_framebuffer_config_validator.sv"
    "$ROOT/fpga/arty/graphics/astra_tile_config_validator.sv"
    "$ROOT/fpga/arty/graphics/astra_sprite_scene_store.sv"
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_store.sv"
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_builder.sv"
    "$ROOT/fpga/arty/graphics/astra_tile_span_walker.sv"
    "$ROOT/fpga/arty/graphics/astra_tile_line_store.sv"
    "$ROOT/fpga/arty/graphics/astra_tile_line_builder.sv"
    "$ROOT/fpga/arty/graphics/astra_sprite_line_store.sv"
    "$ROOT/fpga/arty/graphics/astra_sprite_line_builder.sv"
    "$ROOT/fpga/arty/graphics/astra_line_scheduler.sv"
    "$ROOT/fpga/arty/graphics/astra_palette_store.sv"
    "$ROOT/fpga/arty/graphics/astra_premult_blend.sv"
    "$ROOT/fpga/arty/graphics/astra_pixel_compositor.sv"
    "$ROOT/fpga/arty/graphics/astra_boot_text_overlay.sv"
    "$ROOT/fpga/arty/graphics/astra_axi_lite_1to2.sv"
    "$ROOT/fpga/arty/graphics/astra_copper.sv"
    "$ROOT/fpga/arty/graphics/astra_copper_control.sv"
    "$ROOT/fpga/arty/graphics/astra_copper_beam_scheduler.sv"
    "$ROOT/fpga/arty/graphics/astra_copper_registers.sv"
    "$ROOT/fpga/arty/graphics/astra_copper_structural_state.sv"
    "$ROOT/fpga/arty/common/astra_async_fifo.sv"
    "$ROOT/fpga/arty/graphics/astra_copper_pixel_events.sv"
    "$ROOT/fpga/arty/graphics/astra_graphics_control.sv"
    "$ROOT/fpga/arty/graphics/astra_render_surface_validator.sv"
    "$ROOT/fpga/arty/graphics/astra_render_pixel_writer.sv"
    "$ROOT/fpga/arty/graphics/astra_render_copy_burst.sv"
    "$ROOT/fpga/arty/graphics/astra_render_blitter.sv"
    "$ROOT/fpga/arty/graphics/astra_render_geometry.sv"
    "$ROOT/fpga/arty/graphics/astra_render_flood.sv"
    "$ROOT/fpga/arty/graphics/astra_render_glyph.sv"
    "$ROOT/fpga/arty/graphics/astra_render_command_processor.sv"
    "$ROOT/fpga/arty/graphics/astra_graphics_pipeline.sv"
    "$ROOT/fpga/arty/graphics/sim/astra_render_axi_memory_model.sv"
    "$ROOT/fpga/arty/graphics/sim/tb_astra_graphics_pipeline.sv"
)

iverilog -g2012 -Wall -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_graphics_pipeline \
    -o "$BUILD/tb_astra_graphics_pipeline" "${PIPELINE_SOURCES[@]}"

(cd "$ROOT" && vvp "$BUILD/tb_astra_graphics_pipeline")

iverilog -g2012 -Wall -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_graphics_pipeline \
    -Ptb_astra_graphics_pipeline.OUTPUT_WIDTH=1280 \
    -Ptb_astra_graphics_pipeline.TOTAL_WIDTH=1650 \
    -o "$BUILD/tb_astra_graphics_pipeline_screen_width" \
    "${PIPELINE_SOURCES[@]}"

(cd "$ROOT" && vvp "$BUILD/tb_astra_graphics_pipeline_screen_width")

iverilog -g2012 -Wall -Wno-timescale \
    -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_render_surface_validator \
    -o "$BUILD/tb_astra_render_surface_validator" \
    "$ROOT/fpga/arty/graphics/astra_render_surface_validator.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_surface_validator.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_render_surface_validator")

iverilog -g2012 -Wall -Wno-timescale \
    -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_render_pixel_writer \
    -o "$BUILD/tb_astra_render_pixel_writer" \
    "$ROOT/fpga/arty/graphics/astra_render_pixel_writer.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_pixel_writer.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_render_pixel_writer")

iverilog -g2012 -Wall -Wno-timescale \
    -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_render_blitter \
    -o "$BUILD/tb_astra_render_blitter" \
    "$ROOT/fpga/arty/graphics/astra_render_pixel_writer.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_copy_burst.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_blitter.sv" \
    "$ROOT/fpga/arty/graphics/sim/astra_render_axi_memory_model.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_blitter.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_render_blitter")

iverilog -g2012 -Wall -Wno-timescale \
    -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_render_geometry \
    -o "$BUILD/tb_astra_render_geometry" \
    "$ROOT/fpga/arty/graphics/astra_render_geometry.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_geometry.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_render_geometry")

iverilog -g2012 -Wall -Wno-timescale \
    -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_render_flood \
    -o "$BUILD/tb_astra_render_flood" \
    "$ROOT/fpga/arty/graphics/astra_render_pixel_writer.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_flood.sv" \
    "$ROOT/fpga/arty/graphics/sim/astra_render_axi_memory_model.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_flood.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_render_flood")

iverilog -g2012 -Wall -Wno-timescale \
    -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_render_glyph \
    -o "$BUILD/tb_astra_render_glyph" \
    "$ROOT/fpga/arty/graphics/astra_render_pixel_writer.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_glyph.sv" \
    "$ROOT/fpga/arty/graphics/sim/astra_render_axi_memory_model.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_glyph.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_render_glyph")

iverilog -g2012 -Wall -Wno-timescale \
    -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_render_command_processor \
    -o "$BUILD/tb_astra_render_command_processor" \
    "$ROOT/fpga/arty/graphics/astra_render_surface_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_pixel_writer.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_copy_burst.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_blitter.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_geometry.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_flood.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_glyph.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_command_processor.sv" \
    "$ROOT/fpga/arty/graphics/sim/astra_render_axi_memory_model.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_command_processor.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_render_command_processor")
