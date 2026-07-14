#!/bin/bash
set -euo pipefail

ROOT=$(git rev-parse --show-toplevel)
CPU_CORE=${CPU_CORE:-tg68k030_mmu2}
source ~/oss-cad-suite/environment 2>/dev/null || true

case "$CPU_CORE" in
    wf68k|wf68k30l)
        CPU_CORE=wf68k
        CPU_ARGS=()
        ;;
    tg68k|tg68k_c)
        CPU_CORE=tg68k
        CPU_ARGS=(-GCPU_TG68K=1)
        ;;
    tg68k030|tg68k_mmu|tg68k_c_030_mmu)
        CPU_CORE=tg68k030
        CPU_ARGS=(-GCPU_TG68K=1 -GCPU_TG68K030=1)
        ;;
    tg68k030_mmu2|tg68k_mmu2|tg68k_c_030_mmu2)
        CPU_CORE=tg68k030_mmu2
        CPU_ARGS=(-GCPU_TG68K=1 -GCPU_TG68K030=1 -GCPU_TG68K030_MMU2=1)
        ;;
    *)
        echo "unknown CPU_CORE '$CPU_CORE'" >&2
        exit 2
        ;;
esac

make -C "$ROOT/sw/harte" clean all CPU_CORE="$CPU_CORE"

cd "$ROOT/fpga/soc/sim"
cp ../../../sw/harte/rom_harness.hex rom_init.hex
CPU_CORE="$CPU_CORE" CORE_OUT=harte_core.v bash mkcore.sh

verilator --binary -j 0 --Mdir obj_dir_harte --top-module tb_soc \
    -Wno-lint -Wno-UNOPTFLAT --timing "${CPU_ARGS[@]}" \
    tb_soc.sv ../astra_soc.sv ../boot_memory_map.sv ../tg68k_cache_store.sv \
    ../uart_tx.sv ../uart_rx.sv ../uart_rx_fifo.sv ../spi_sd.sv \
    ../astra_host_async_byte_fifo.sv ../astra_host_spi_slave.sv \
    ../astra_host_boot.sv harte_core.v

./obj_dir_harte/Vtb_soc
