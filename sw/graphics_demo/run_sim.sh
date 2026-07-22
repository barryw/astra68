#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
mode="${1:-normal}"
base_cflags="${EXTRA_CFLAGS:-}"
cd "$repo_root"
source ~/oss-cad-suite/environment 2>/dev/null || true

build_rom() {
    local label="$1"
    local mode_cflags="$2"
    local linker_script="${3:-../boot/astra_st.ld}"
    local demo_source="${4:-demo.c}"
    echo "[graphics-demo] build $label"
    make -C sw/graphics_demo clean all \
        EXTRA_CFLAGS="$base_cflags $mode_cflags" \
        LINKER_SCRIPT="$linker_script" DEMO_SOURCE="$demo_source"
    cp sw/graphics_demo/astra_graphics_demo.hex fpga/soc/sim/rom_init.hex
}

run_rom() {
    local label="$1"
    echo "[graphics-demo] run $label"
    pushd fpga/soc/sim >/dev/null
    ./obj_dir_graphics_demo/Vtb_graphics_demo
    popd >/dev/null
}

case "$mode" in
    normal|all)
        build_rom normal ""
        ;;
    hardware-map)
        build_rom hardware-map "" ../stage0/stage0.ld
        ;;
    blit-config)
        build_rom blit-config "" ../stage0/stage0.ld blit_diag.c
        ;;
    stress-index8)
        build_rom stress-index8 "-DDEMO_STRESS_SPRITES=1"
        ;;
    stress-rgb565)
        build_rom stress-rgb565 \
            "-DDEMO_STRESS_SPRITES=1 -DDEMO_STRESS_RGB565=1"
        ;;
    *)
        echo "usage: $0 [normal|hardware-map|blit-config|stress-index8|stress-rgb565|all]" >&2
        exit 2
        ;;
esac

cp fpga/soc/post_fonts.hex fpga/soc/sim/post_fonts.hex

cd fpga/soc/sim
rm -rf obj_dir_graphics_demo graphics_demo_core.v
CORE_OUT=graphics_demo_core.v bash mkcore.sh
verilator_params=()
if [[ "$mode" == hardware-map || "$mode" == blit-config ]]; then
    verilator_params+=("-GPRODUCTION_MAP=1")
fi
if [[ "$mode" == blit-config ]]; then
    verilator_params+=("-GDIAGNOSTIC_ONLY=1")
fi
verilator --binary -j 0 --Mdir obj_dir_graphics_demo \
    --top-module tb_graphics_demo -Wno-lint -Wno-UNOPTFLAT --timing \
    "${verilator_params[@]}" \
    tb_graphics_demo.sv tb_sdram32_controller.sv ecp5pll_sim.sv \
    ../astra_soc.sv ../astra_front_panel.sv ../vesta_irq_timer.sv ../boot_memory_map.sv \
    ../tg68k_cache_store.sv ../post_console.sv \
    ../astraea_blitter.sv ../astraea_pixel_port.sv ../astraea_draw.sv \
    ../astraea_copper.sv ../astraea_chip.sv \
    ../vega_sprite_builder.sv ../vega_video.sv \
    ../uart_tx.sv ../uart_rx.sv ../uart_rx_fifo.sv ../spi_sd.sv \
    ../astra_host_async_byte_fifo.sv ../astra_host_spi_slave.sv \
    ../astra_async_fifo.sv ../astra_host_runtime.sv \
    ../astra_host_service.sv ../sdram32_controller.sv \
    ../sdram32_cpu_bridge.sv ../sdram32_bist.sv \
    ../thirdparty/core_sdram_axi4/sdram_axi_core.v graphics_demo_core.v

cd "$repo_root"
run_rom "${mode/all/normal}"

if [[ "$mode" == all ]]; then
    build_rom stress-index8 "-DDEMO_STRESS_SPRITES=1"
    run_rom stress-index8
    build_rom stress-rgb565 \
        "-DDEMO_STRESS_SPRITES=1 -DDEMO_STRESS_RGB565=1"
    run_rom stress-rgb565
fi
