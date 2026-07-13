#!/bin/bash
# Build the boot ROM and require its complete banner from the SoC UART.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/../.."
source ~/oss-cad-suite/environment 2>/dev/null || true

CPU_CORE="${CPU_CORE:-wf68k}"
verilator_cpu_args=()
boot_cpu_cflags=()
case "$CPU_CORE" in
    wf68k|wf68k30l)
        CPU_CORE="wf68k"
        ;;
    tg68k|tg68k_c)
        CPU_CORE="tg68k"
        verilator_cpu_args=(-GCPU_TG68K=1)
        boot_cpu_cflags=(-DASTRA_CPU_TG68K=1)
        ;;
    tg68k030|tg68k_mmu|tg68k_c_030_mmu)
        CPU_CORE="tg68k030"
        verilator_cpu_args=(-GCPU_TG68K=1 -GCPU_TG68K030=1)
        boot_cpu_cflags=(-DASTRA_CPU_TG68K030=1)
        ;;
    tg68k030_mmu2|tg68k_mmu2|tg68k_c_030_mmu2)
        CPU_CORE="tg68k030_mmu2"
        verilator_cpu_args=(-GCPU_TG68K=1 -GCPU_TG68K030=1 -GCPU_TG68K030_MMU2=1)
        boot_cpu_cflags=(-DASTRA_CPU_TG68K030=1)
        ;;
    *)
        echo "unknown CPU_CORE='$CPU_CORE' (expected wf68k, tg68k, tg68k030, or tg68k030_mmu2)" >&2
        exit 2
        ;;
esac

echo "boot simulation CPU_CORE=$CPU_CORE"
make -C sw/boot clean all CPU_CORE="$CPU_CORE" CPU_CLK_DIV_BIT=0 \
    EXTRA_CFLAGS="${boot_cpu_cflags[*]:-}"
python3 sw/boot/bin2hex.py sw/boot/astra_boot.bin sw/boot/rom_boot.hex

cd fpga/soc/sim
rm -rf obj_dir_boot boot_core.v
cp ../../../sw/boot/rom_boot.hex rom_init.hex
CPU_CORE="$CPU_CORE" CORE_OUT=boot_core.v bash mkcore.sh
verilator --binary -j 0 --Mdir obj_dir_boot --top-module tb_boot \
    -Wno-lint -Wno-UNOPTFLAT --timing \
    "${verilator_cpu_args[@]}" \
    tb_boot.sv ../astra_soc.sv ../tg68k_cache_store.sv ../uart_tx.sv ../uart_rx.sv boot_core.v
./obj_dir_boot/Vtb_boot
