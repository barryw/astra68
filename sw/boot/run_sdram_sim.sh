#!/bin/bash
# Build and run the TG030 boot ROM against the pin-level 32 MiB SDRAM model.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
cd "$repo_root"
source ~/oss-cad-suite/environment 2>/dev/null || true

mem_bench_bytes="${MEM_BENCH_BYTES:-256}"
dma_bench_bytes="${DMA_BENCH_BYTES:-1024}"
test_bytes="${SDRAM_SIM_TEST_BYTES:-65536}"
progress="${SDRAM_SIM_PROGRESS:-0}"
reuse_sim="${SDRAM_SIM_REUSE:-0}"
kernel_panic_selftest="${KERNEL_PANIC_SELFTEST:-0}"
kernel_sched_trace="${KERNEL_SCHED_TRACE:-0}"
sim_args=()
if [[ "$kernel_panic_selftest" != "0" ]]; then
    sim_args+=(+expect-kernel-panic)
fi

make -C sw/boot clean all \
    CPU_CLK_DIV_BIT=0 SDRAM_ENABLE=1 HDMI_ENABLE=0 \
    KERNEL_PANIC_SELFTEST="$kernel_panic_selftest" \
    KERNEL_SCHED_TRACE="$kernel_sched_trace" \
    EXTRA_CFLAGS="-DMEM_BENCH_BYTES=$mem_bench_bytes -DDMA_BENCH_BYTES=$dma_bench_bytes"
python3 sw/boot/bin2hex.py sw/boot/astra_boot.bin fpga/soc/sim/rom_init.hex
if [[ "$kernel_panic_selftest" == "2" ]]; then
    guard_address=$(m68k-linux-gnu-nm -n sw/kernel/astra_kernel.elf |
        awk '$3 == "_kernel_stack_guard" { print $1 }')
    if [[ ! "$guard_address" =~ ^[0-9a-fA-F]{8}$ ]]; then
        echo "unable to resolve supervisor stack guard address" >&2
        exit 1
    fi
    sim_args+=("+expect-kernel-guard=$guard_address")
fi

cd fpga/soc/sim
if [[ "$reuse_sim" != "1" || ! -x obj_dir_boot_sdram/Vtb_boot_sdram ]]; then
    rm -rf obj_dir_boot_sdram boot_sdram_core.v
    CORE_OUT=boot_sdram_core.v bash mkcore.sh
    verilator --binary -j 0 --Mdir obj_dir_boot_sdram \
        --top-module tb_boot_sdram -Wno-lint -Wno-UNOPTFLAT --timing \
        -GTEST_BYTES="$test_bytes" -GPROGRESS="$progress" \
        tb_boot_sdram.sv tb_sdram32_controller.sv ecp5pll_sim.sv \
        ../astra_soc.sv ../astra_front_panel.sv ../vesta_irq_timer.sv ../boot_memory_map.sv ../tg68k_cache_store.sv \
        ../astraea_blitter.sv ../astraea_pixel_port.sv ../astraea_draw.sv \
        ../astraea_copper.sv ../astraea_chip.sv \
        ../vega_sprite_builder.sv ../vega_video.sv \
        ../uart_tx.sv ../uart_rx.sv ../uart_rx_fifo.sv ../spi_sd.sv \
        ../astra_host_async_byte_fifo.sv ../astra_host_spi_slave.sv \
        ../astra_async_fifo.sv ../astra_host_runtime.sv ../astra_host_service.sv \
        ../sdram32_controller.sv ../sdram32_cpu_bridge.sv ../sdram32_bist.sv \
        ../thirdparty/core_sdram_axi4/sdram_axi_core.v boot_sdram_core.v
else
    echo "reusing obj_dir_boot_sdram/Vtb_boot_sdram (caller guarantees matching RTL/generics)"
fi
./obj_dir_boot_sdram/Vtb_boot_sdram "${sim_args[@]}"
