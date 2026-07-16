#!/bin/bash
# Run the complete stage-0 BRAM -> AstraHost SPI -> SDRAM stage-2 boot path.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
cd "$repo_root"
source ~/oss-cad-suite/environment 2>/dev/null || true

test_bytes="${SDRAM_SIM_TEST_BYTES:-65536}"
progress="${ASTRA_HOST_SIM_PROGRESS:-0}"
reuse_sim="${ASTRA_HOST_SIM_REUSE:-0}"
kernel_panic_selftest="${KERNEL_PANIC_SELFTEST:-0}"
sim_args=()
if [[ "$kernel_panic_selftest" == "1" ]]; then
    sim_args+=(+expect-kernel-panic)
fi

make -C sw/stage0 clean all BOOT_BACKEND=host
make -C sw/boot clean all \
    CPU_CLK_DIV_BIT=0 SDRAM_ENABLE=1 HDMI_ENABLE=0 \
    KERNEL_PANIC_SELFTEST="$kernel_panic_selftest" \
    EXTRA_CFLAGS="-DMEM_BENCH_BYTES=256 -DDMA_BENCH_BYTES=1024"
python3 sw/boot/package_rom.py sw/boot/astra_boot.bin sw/boot/astra68.rom \
    --hex-output fpga/soc/sim/astra68_rom.hex
python3 sw/boot/bin2hex.py \
    sw/stage0/stage0.bin fpga/soc/sim/rom_init.hex

cd fpga/soc/sim
if [[ "$reuse_sim" != "1" || ! -x obj_dir_host_boot/Vtb_astra_host_boot_soc ]]; then
    rm -rf obj_dir_host_boot host_boot_core.v
    CORE_OUT=host_boot_core.v bash mkcore.sh
    verilator --binary -j 0 --Mdir obj_dir_host_boot \
        --top-module tb_astra_host_boot_soc -Wno-lint -Wno-UNOPTFLAT --timing \
        -GTEST_BYTES="$test_bytes" -GPROGRESS="$progress" \
        tb_astra_host_boot_soc.sv tb_sdram32_controller.sv ecp5pll_sim.sv \
        ../astra_soc.sv ../astra_front_panel.sv ../boot_memory_map.sv ../tg68k_cache_store.sv \
        ../astraea_blitter.sv ../astraea_pixel_port.sv ../astraea_draw.sv \
        ../astraea_copper.sv ../astraea_chip.sv \
        ../vega_tile_builder.sv ../vega_sprite_builder.sv ../vega_video.sv \
        ../uart_tx.sv ../uart_rx.sv \
        ../uart_rx_fifo.sv ../spi_sd.sv ../astra_host_async_byte_fifo.sv \
        ../astra_host_spi_slave.sv ../astra_host_boot.sv \
        ../sdram32_controller.sv ../sdram32_cpu_bridge.sv \
        ../sdram32_bist.sv \
        ../thirdparty/core_sdram_axi4/sdram_axi_core.v host_boot_core.v
else
    echo "reusing AstraHost simulation (caller guarantees matching RTL/generics)"
fi
./obj_dir_host_boot/Vtb_astra_host_boot_soc "${sim_args[@]}"
