#!/bin/bash
# Run the complete stage-0 BRAM -> FAT32 SD -> SDRAM stage-2 boot path.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
cd "$repo_root"
source ~/oss-cad-suite/environment 2>/dev/null || true

test_bytes="${SDRAM_SIM_TEST_BYTES:-65536}"
progress="${SD_BOOT_SIM_PROGRESS:-0}"
reuse_sim="${SD_BOOT_SIM_REUSE:-0}"
cpu_core="${CPU_CORE:-tg68k030_mmu2}"
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

make -C sw/stage0 clean all
make -C sw/boot clean all \
    CPU_CORE="$cpu_core" CPU_CLK_DIV_BIT=0 SDRAM_ENABLE=1 HDMI_ENABLE=0 \
    EXTRA_CFLAGS="-DMEM_BENCH_BYTES=256 -DDMA_BENCH_BYTES=1024"
python3 sw/stage0/make_fat32_image.py \
    sw/boot/astra68.rom fpga/soc/sim/sdcard.img
python3 sw/boot/bin2hex.py \
    sw/stage0/stage0.bin fpga/soc/sim/rom_init.hex

cd fpga/soc/sim
if [[ "$reuse_sim" != "1" || ! -x obj_dir_sd_boot/Vtb_sd_boot ]]; then
    rm -rf obj_dir_sd_boot sd_boot_core.v
    CPU_CORE="$cpu_core" CORE_OUT=sd_boot_core.v bash mkcore.sh
    verilator --binary -j 0 --Mdir obj_dir_sd_boot \
        --top-module tb_sd_boot -Wno-lint -Wno-UNOPTFLAT --timing \
        "${verilator_cpu_args[@]}" \
        -GTEST_BYTES="$test_bytes" -GPROGRESS="$progress" \
        tb_sd_boot.sv sd_card_spi_model.sv tb_sdram32_controller.sv \
        ecp5pll_sim.sv ../astra_soc.sv ../boot_memory_map.sv \
        ../tg68k_cache_store.sv ../astraea_blitter.sv ../uart_tx.sv \
        ../uart_rx.sv ../uart_rx_fifo.sv ../spi_sd.sv \
        ../astra_host_async_byte_fifo.sv ../astra_host_spi_slave.sv \
        ../astra_host_boot.sv \
        ../sdram32_controller.sv ../sdram32_cpu_bridge.sv \
        ../sdram32_bist.sv \
        ../thirdparty/core_sdram_axi4/sdram_axi_core.v sd_boot_core.v
else
    echo "reusing obj_dir_sd_boot/Vtb_sd_boot (caller guarantees matching RTL/generics)"
fi
./obj_dir_sd_boot/Vtb_sd_boot
