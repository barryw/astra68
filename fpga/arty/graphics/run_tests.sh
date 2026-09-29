#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BUILD=${BUILD:-"$ROOT/build/arty-graphics"}
BOOT_FONT="$ROOT/build/arty-graphics/post_fonts.hex"

mkdir -p "$BUILD" "$(dirname "$BOOT_FONT")"
python3 "$ROOT/tools/fonts/test_afnt.py"
python3 "$ROOT/tools/fonts/afnt.py" emit-cp437-hex \
    "$ROOT/sw/userspace/graphics/fonts/astra-rescue-mono.afnt" "$BOOT_FONT"
python3 "$ROOT/fpga/arty/graphics/protocol/generate_protocol.py"
python3 "$ROOT/fpga/arty/graphics/test_hdmi_source_contract.py"

iverilog -g2012 -Wall \
    -s tb_video_timing \
    -o "$BUILD/tb_video_timing" \
    "$ROOT/third_party/hdl-util-hdmi/video_timing.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_video_timing.sv"

vvp "$BUILD/tb_video_timing"

iverilog -g2012 -Wall \
    -s tb_video_timing_1080p \
    -o "$BUILD/tb_video_timing_1080p" \
    "$ROOT/third_party/hdl-util-hdmi/video_timing.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_video_timing_1080p.sv"

vvp "$BUILD/tb_video_timing_1080p"

iverilog -g2012 -Wall \
    -s tb_astra_access_fault_record \
    -o "$BUILD/tb_astra_access_fault_record" \
    "$ROOT/fpga/arty/graphics/astra_access_fault_record.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_access_fault_record.sv"

vvp "$BUILD/tb_astra_access_fault_record"

iverilog -g2012 -Wall \
    -s tb_astra_display_axis_scaler \
    -o "$BUILD/tb_astra_display_axis_scaler" \
    "$ROOT/fpga/arty/graphics/astra_display_axis_scaler.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_display_axis_scaler.sv"

vvp "$BUILD/tb_astra_display_axis_scaler"

iverilog -g2012 -Wall \
    -s tb_astra_scanline_replay \
    -o "$BUILD/tb_astra_scanline_replay" \
    "$ROOT/fpga/arty/graphics/astra_scanline_replay.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_scanline_replay.sv"

vvp "$BUILD/tb_astra_scanline_replay"

iverilog -g2012 -Wall \
    -s tb_astra_hardware_pointer \
    -o "$BUILD/tb_astra_hardware_pointer" \
    "$ROOT/fpga/arty/graphics/astra_pixel_compositor.sv" \
    "$ROOT/fpga/arty/graphics/astra_hardware_pointer.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_hardware_pointer.sv"

vvp "$BUILD/tb_astra_hardware_pointer"

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
    -s tb_astra_display_capture \
    -o "$BUILD/tb_astra_display_capture" \
    "$ROOT/fpga/arty/common/astra_async_fifo.sv" \
    "$ROOT/fpga/arty/graphics/astra_access_fault_record.sv" \
    "$ROOT/fpga/arty/graphics/astra_display_capture.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_display_capture.sv"

vvp "$BUILD/tb_astra_display_capture"

iverilog -g2012 -Wall \
    -s tb_astra_front_panel_axi \
    -o "$BUILD/tb_astra_front_panel_axi" \
    "$ROOT/fpga/arty/common/astra_front_panel.sv" \
    "$ROOT/fpga/arty/graphics/astra_access_fault_record.sv" \
    "$ROOT/fpga/arty/rtl/astra_front_panel_axi.sv" \
    "$ROOT/fpga/arty/rtl/sim/tb_astra_front_panel_axi.sv"

vvp "$BUILD/tb_astra_front_panel_axi"

iverilog -g2012 -Wall \
    -s tb_astra_boot_text_overlay \
    -o "$BUILD/tb_astra_boot_text_overlay" \
    "$ROOT/fpga/arty/graphics/astra_boot_text_overlay.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_boot_text_overlay.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_boot_text_overlay")


iverilog -g2012 -Wall -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_framebuffer_line_builder \
    -o "$BUILD/tb_astra_framebuffer_line_builder" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_store.sv" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_builder.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_framebuffer_line_builder.sv"

vvp "$BUILD/tb_astra_framebuffer_line_builder"

iverilog -g2012 -Wall -I "$ROOT/fpga/arty/graphics" \
    -Ptb_astra_framebuffer_line_builder.AXI_DATA_WIDTH=128 \
    -s tb_astra_framebuffer_line_builder \
    -o "$BUILD/tb_astra_framebuffer_line_builder_128" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_store.sv" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_builder.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_framebuffer_line_builder.sv"

vvp "$BUILD/tb_astra_framebuffer_line_builder_128"

iverilog -g2012 -Wall -I "$ROOT/fpga/arty/graphics" \
    -Ptb_astra_framebuffer_line_builder.SCENE_PERF_MODE=1 \
    -s tb_astra_framebuffer_line_builder \
    -o "$BUILD/tb_astra_framebuffer_scene_perf" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_config_validator.sv" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_store.sv" \
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_builder.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_framebuffer_line_builder.sv"

vvp "$BUILD/tb_astra_framebuffer_scene_perf"

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
    "$ROOT/fpga/arty/graphics/astra_display_axis_scaler.sv" \
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
    "$ROOT/fpga/arty/graphics/astra_access_fault_record.sv" \
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
    "$ROOT/fpga/arty/graphics/astra_sprite_scene_store.sv" \
    "$ROOT/fpga/arty/graphics/astra_access_fault_record.sv" \
    "$ROOT/fpga/arty/graphics/astra_graphics_control.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_graphics_control.sv"

vvp "$BUILD/tb_astra_graphics_control"

PIPELINE_SOURCES=(
    "$ROOT/fpga/arty/graphics/astra_framebuffer_config_validator.sv"
    "$ROOT/fpga/arty/graphics/astra_sprite_scene_store.sv"
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_store.sv"
    "$ROOT/fpga/arty/graphics/astra_framebuffer_line_builder.sv"
    "$ROOT/fpga/arty/graphics/astra_sprite_line_store.sv"
    "$ROOT/fpga/arty/graphics/astra_sprite_line_builder.sv"
    "$ROOT/fpga/arty/graphics/astra_display_axis_scaler.sv"
    "$ROOT/fpga/arty/graphics/astra_scanline_replay.sv"
    "$ROOT/fpga/arty/graphics/astra_hardware_pointer.sv"
    "$ROOT/fpga/arty/graphics/astra_line_scheduler.sv"
    "$ROOT/fpga/arty/graphics/astra_palette_store.sv"
    "$ROOT/fpga/arty/graphics/astra_premult_blend.sv"
    "$ROOT/fpga/arty/graphics/astra_pixel_compositor.sv"
    "$ROOT/fpga/arty/graphics/astra_boot_text_overlay.sv"
    "$ROOT/fpga/arty/graphics/astra_axi_lite_1to2.sv"
    "$ROOT/fpga/arty/graphics/astra_copper.sv"
    "$ROOT/fpga/arty/graphics/astra_access_fault_record.sv"
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
    "$ROOT/fpga/arty/graphics/astra_render_texture.sv"
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
    -Ptb_astra_graphics_pipeline.OUTPUT_WIDTH=1920 \
    -Ptb_astra_graphics_pipeline.TOTAL_WIDTH=2200 \
    -Ptb_astra_graphics_pipeline.FRAMEBUFFER_AXI_DATA_WIDTH=128 \
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

# Texture engine: the C reference model is the oracle. Its own hand-computed
# self-test runs first, then it generates the vectors the RTL must match.
TEXTURE_BUILD="$BUILD/texture"
TEXTURE_CFLAGS=(-std=c11 -O2 -Wall -Wextra -Werror
    -I "$ROOT/sw/userspace/graphics/include" -I "$ROOT/sw/include"
    -I "$ROOT/fpga/arty/linux")
mkdir -p "$TEXTURE_BUILD"
cc "${TEXTURE_CFLAGS[@]}" \
    "$ROOT/sw/userspace/graphics/src/texture_reference.c" \
    "$ROOT/sw/userspace/graphics/tests/test_texture_reference.c" \
    -o "$TEXTURE_BUILD/test_texture_reference"
"$TEXTURE_BUILD/test_texture_reference"
cc "${TEXTURE_CFLAGS[@]}" \
    "$ROOT/sw/userspace/graphics/src/texture_reference.c" \
    "$ROOT/fpga/arty/graphics/sim/texture_vectors.c" \
    -o "$TEXTURE_BUILD/texture_vectors"
"$TEXTURE_BUILD/texture_vectors" "$TEXTURE_BUILD"

iverilog -g2012 -Wall -Wno-timescale \
    -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_render_texture \
    -o "$BUILD/tb_astra_render_texture" \
    "$ROOT/fpga/arty/graphics/astra_render_pixel_writer.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_texture.sv" \
    "$ROOT/fpga/arty/graphics/sim/astra_render_axi_memory_model.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_texture.sv"

(cd "$TEXTURE_BUILD" && vvp "$BUILD/tb_astra_render_texture")

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
    "$ROOT/fpga/arty/graphics/astra_render_texture.sv" \
    "$ROOT/fpga/arty/graphics/astra_render_command_processor.sv" \
    "$ROOT/fpga/arty/graphics/sim/astra_render_axi_memory_model.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_command_processor.sv"

(cd "$ROOT" && vvp "$BUILD/tb_astra_render_command_processor")

iverilog -g2012 -Wall -I "$ROOT/fpga/arty/graphics" \
    -s tb_astra_render_host_reads \
    -o "$BUILD/tb_astra_render_host_reads" \
    "$ROOT/fpga/arty/graphics/astra_render_host_reads.sv" \
    "$ROOT/fpga/arty/graphics/sim/astra_render_axi_latency_model.sv" \
    "$ROOT/fpga/arty/graphics/sim/tb_astra_render_host_reads.sv"

vvp "$BUILD/tb_astra_render_host_reads"

# Latency-model bench: axis-aligned TRIANGLES quads cover each pixel exactly
# once (ADD on black). The timing cases run from perf/run_perf.sh.
BUILD="$BUILD" ROOT="$ROOT" "$ROOT/fpga/arty/graphics/perf/run_perf.sh" 25 25 80

# Memory ordering of posted writes: slow, reorderable write responses.
BUILD="$BUILD" ROOT="$ROOT" "$ROOT/fpga/arty/graphics/perf/run_ordering.sh"
