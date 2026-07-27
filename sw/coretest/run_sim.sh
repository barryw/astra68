#!/bin/bash
# Build and run the coretest ROM in the SoC netlist simulation.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/../.."
source ~/oss-cad-suite/environment 2>/dev/null || true

verilator_debug_args=()
verilator_define_args=()
verilator_sdram_args=()
verilator_sdram_sources=()
coretest_cpu_cflags=()
coretest_fb_guard="${CORETEST_FB_GUARD:-0}"
if [[ "${CORETEST_DEBUG_IRQ:-0}" == "1" ]]; then
    verilator_debug_args+=(-GDEBUG_IRQ_PARAM=1)
fi
if [[ "${CORETEST_DEBUG_PMMU:-0}" == "1" ]]; then
    verilator_debug_args+=(-GDEBUG_PMMU_PARAM=1)
fi
if [[ "${CORETEST_DEBUG_CHK:-0}" == "1" ]]; then
    verilator_debug_args+=(-GDEBUG_CHK_PARAM=1)
fi
if [[ "${CORETEST_SIM_PROGRESS:-0}" == "1" ]]; then
    coretest_cpu_cflags+=(-DCORETEST_SIM_PROGRESS=1)
fi
if [[ "${CORETEST_TRAP_EXCEPTION_MARKERS:-0}" == "1" ]]; then
    verilator_debug_args+=(-GTRAP_EXCEPTION_MARKERS_PARAM=1)
fi
if [[ -n "${CORETEST_SIM_TIMEOUT_PS:-}" ]]; then
    verilator_debug_args+=(-GSIM_TIMEOUT_PS_PARAM="$CORETEST_SIM_TIMEOUT_PS")
fi
if [[ "${CORETEST_SDRAM_BERR:-0}" == "1" || "$coretest_fb_guard" == "1" ]]; then
    coretest_cpu_cflags+=(-DCORETEST_SIM_FOCUS_SDRAM_BERR=1)
    verilator_define_args+=(-DCORETEST_SIM_BUS_FAULTS)
    verilator_sdram_args+=(-GSDRAM_ENABLE_PARAM=1)
    verilator_sdram_sources=(
        ecp5pll_sim.sv
        ../astraea_blitter.sv
        ../astraea_pixel_port.sv
        ../astraea_draw.sv
        ../astraea_copper.sv
        ../astraea_chip.sv
        ../sdram32_controller.sv
        ../sdram32_cpu_bridge.sv
        ../sdram32_bist.sv
        ../thirdparty/core_sdram_axi4/sdram_axi_core.v
    )
fi
if [[ "$coretest_fb_guard" == "1" ]]; then
    coretest_cpu_cflags+=(-DCORETEST_SIM_FB_GUARD=1)
    verilator_define_args+=(-DCORETEST_SIM_FB_GUARD)
    verilator_sdram_args+=(-GHDMI_ENABLE_PARAM=1)
    verilator_sdram_sources+=(../post_console.sv)
    cp fpga/soc/post_fonts.hex fpga/soc/sim/post_fonts.hex
fi
echo "coretest CPU=TG68K030_MMU2"
sim_cflags="-DCORETEST_SIM_IRQ ${coretest_cpu_cflags[*]:-} ${CORETEST_EXTRA_CFLAGS:-}"

make -C sw/coretest clean all EXTRA_CFLAGS="$sim_cflags"
if [[ "$sim_cflags" != *CORETEST_SIM_FOCUS* ]]; then
    make -C sw/coretest check-disasm EXTRA_CFLAGS="$sim_cflags"
fi

cd fpga/soc/sim
if [[ "${CORETEST_REUSE_SIM:-0}" != "1" || ! -x obj_dir/Vtb_coretest ]]; then
    rm -rf obj_dir astra_cpu_core.v
    bash mkcore.sh
    verilator --binary -j 0 --top-module tb_coretest -Wno-lint -Wno-UNOPTFLAT --timing -DASTRA_SOC_SIM_IRQ \
        "${verilator_define_args[@]}" \
        "${verilator_debug_args[@]}" \
        "${verilator_sdram_args[@]}" \
        tb_coretest.sv tb_sdram32_controller.sv ../astra_soc.sv ../astra_front_panel.sv ../vesta_irq_timer.sv \
        ../vesta_bus_fault.sv \
        ../tg68k_cache_store.sv ../vega_sprite_builder.sv \
        ../vega_video.sv \
        ../boot_memory_map.sv ../uart_tx.sv ../uart_rx.sv ../uart_rx_fifo.sv ../spi_sd.sv \
        ../astra_host_async_byte_fifo.sv ../astra_host_spi_slave.sv \
        ../astra_async_fifo.sv ../astra_host_runtime.sv \
        ../astra_host_service.sv "${verilator_sdram_sources[@]}" astra_cpu_core.v
fi
cp ../../../sw/coretest/rom_coretest.hex rom_init.hex
./obj_dir/Vtb_coretest
