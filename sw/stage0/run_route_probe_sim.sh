#!/bin/bash
# Run the diagnostic stage-0 image without SDRAM, SD, or AstraHost.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
cd "$repo_root"
source ~/oss-cad-suite/environment 2>/dev/null || true

make -C sw/stage0 clean route-probe
python3 sw/boot/bin2hex.py \
    sw/stage0/route_probe.bin fpga/soc/sim/rom_init.hex

cd fpga/soc/sim
rm -rf obj_dir_route_probe route_probe_core.v
CORE_OUT=route_probe_core.v bash mkcore.sh
verilator --binary -j 0 --Mdir obj_dir_route_probe \
    --top-module tb_route_probe -Wno-lint -Wno-UNOPTFLAT --timing \
    tb_route_probe.sv ecp5pll_sim.sv \
    ../astra_soc.sv ../astra_front_panel.sv ../vesta_irq_timer.sv ../boot_memory_map.sv \
    ../tg68k_cache_store.sv ../astraea_blitter.sv \
    ../astraea_pixel_port.sv ../astraea_draw.sv ../astraea_copper.sv \
    ../astraea_chip.sv \
    ../vega_sprite_builder.sv ../vega_video.sv ../uart_tx.sv \
    ../uart_rx.sv ../uart_rx_fifo.sv ../spi_sd.sv \
    ../astra_host_async_byte_fifo.sv ../astra_host_spi_slave.sv \
    ../astra_async_fifo.sv ../astra_host_runtime.sv \
    ../astra_host_service.sv ../sdram32_controller.sv \
    ../sdram32_cpu_bridge.sv ../sdram32_bist.sv \
    ../thirdparty/core_sdram_axi4/sdram_axi_core.v route_probe_core.v
./obj_dir_route_probe/Vtb_route_probe "$@"
