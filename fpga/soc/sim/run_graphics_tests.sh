#!/bin/sh
# Directed Vega/Astraea/SDRAM-port regression. Keep this list aligned with the
# graphics source list in ../oss_flow/mkbit.sh.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOC_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/build/graphics"}
IVERILOG=${IVERILOG:-iverilog}
VVP=${VVP:-vvp}

mkdir -p "$BUILD_DIR"

run_test() {
    name=$1
    shift
    echo "[graphics] $name"
    "$IVERILOG" -g2012 -Wall -Wno-timescale -s "$name" \
        -o "$BUILD_DIR/$name" "$@" "$SCRIPT_DIR/$name.sv"
    "$VVP" "$BUILD_DIR/$name"
}

run_test tb_sdram32_controller \
    "$SOC_DIR/thirdparty/core_sdram_axi4/sdram_axi_core.v" \
    "$SOC_DIR/sdram32_controller.sv"
run_test tb_astraea_blitter \
    "$SOC_DIR/thirdparty/core_sdram_axi4/sdram_axi_core.v" \
    "$SOC_DIR/sdram32_controller.sv" \
    "$SCRIPT_DIR/tb_sdram32_controller.sv" \
    "$SOC_DIR/astraea_blitter.sv"
run_test tb_astraea_copper \
    "$SOC_DIR/astraea_copper.sv"
run_test tb_astraea_draw \
    "$SOC_DIR/astraea_pixel_port.sv" \
    "$SOC_DIR/astraea_draw.sv"
run_test tb_astraea_chip \
    "$SOC_DIR/astraea_blitter.sv" \
    "$SOC_DIR/astraea_pixel_port.sv" \
    "$SOC_DIR/astraea_draw.sv" \
    "$SOC_DIR/astraea_copper.sv" \
    "$SOC_DIR/astraea_chip.sv"
run_test tb_vega_tile_builder \
    "$SOC_DIR/vega_tile_builder.sv"
run_test tb_vega_sprite_builder \
    "$SOC_DIR/vega_sprite_builder.sv"
run_test tb_vega_video \
    "$SOC_DIR/vega_tile_builder.sv" \
    "$SOC_DIR/vega_sprite_builder.sv" \
    "$SOC_DIR/vega_video.sv"

echo "[graphics] all directed tests passed"
