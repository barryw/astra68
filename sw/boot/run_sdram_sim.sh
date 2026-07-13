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
cpu_core="${CPU_CORE:-tg68k030}"
verilator_cpu_args=()

case "$cpu_core" in
    tg68k030|tg68k_mmu|tg68k_c_030_mmu)
        cpu_core="tg68k030"
        ;;
    tg68k030_mmu2|tg68k_mmu2|tg68k_c_030_mmu2)
        cpu_core="tg68k030_mmu2"
        verilator_cpu_args=(-GCPU_MMU2=1)
        ;;
    *)
        echo "unknown CPU_CORE='$cpu_core' (expected tg68k030 or tg68k030_mmu2)" >&2
        exit 2
        ;;
esac

make -C sw/boot clean all \
    CPU_CORE="$cpu_core" CPU_CLK_DIV_BIT=0 SDRAM_ENABLE=1 HDMI_ENABLE=0 \
    EXTRA_CFLAGS="-DMEM_BENCH_BYTES=$mem_bench_bytes -DDMA_BENCH_BYTES=$dma_bench_bytes"
python3 sw/boot/bin2hex.py sw/boot/astra_boot.bin fpga/soc/sim/rom_init.hex

cd fpga/soc/sim
if [[ "$reuse_sim" != "1" || ! -x obj_dir_boot_sdram/Vtb_boot_sdram ]]; then
    rm -rf obj_dir_boot_sdram boot_sdram_core.v
    CPU_CORE="$cpu_core" CORE_OUT=boot_sdram_core.v bash mkcore.sh
    verilator --binary -j 0 --Mdir obj_dir_boot_sdram \
        --top-module tb_boot_sdram -Wno-lint -Wno-UNOPTFLAT --timing \
        "${verilator_cpu_args[@]}" \
        -GTEST_BYTES="$test_bytes" -GPROGRESS="$progress" \
        tb_boot_sdram.sv tb_sdram32_controller.sv ecp5pll_sim.sv \
        ../astra_soc.sv ../tg68k_cache_store.sv ../astraea_blitter.sv ../uart_tx.sv ../uart_rx.sv \
        ../sdram32_controller.sv ../sdram32_cpu_bridge.sv ../sdram32_bist.sv \
        ../thirdparty/core_sdram_axi4/sdram_axi_core.v boot_sdram_core.v
else
    echo "reusing obj_dir_boot_sdram/Vtb_boot_sdram (caller guarantees matching RTL/generics)"
fi
./obj_dir_boot_sdram/Vtb_boot_sdram
