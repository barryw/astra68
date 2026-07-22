#!/bin/bash
# Build the boot ROM and require its complete banner from the SoC UART.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/../.."
source ~/oss-cad-suite/environment 2>/dev/null || true

echo "boot simulation CPU=TG68K030_MMU2"
make -C sw/boot clean all CPU_CLK_DIV_BIT=0
python3 sw/boot/bin2hex.py sw/boot/astra_boot.bin sw/boot/rom_boot.hex

cd fpga/soc/sim
rm -rf obj_dir_boot boot_core.v
cp ../../../sw/boot/rom_boot.hex rom_init.hex
CORE_OUT=boot_core.v bash mkcore.sh
verilator --binary -j 0 --Mdir obj_dir_boot --top-module tb_boot \
    -Wno-lint -Wno-UNOPTFLAT --timing \
    tb_boot.sv ../astra_soc.sv ../astra_front_panel.sv ../vesta_irq_timer.sv ../boot_memory_map.sv ../tg68k_cache_store.sv \
    ../astraea_blitter.sv ../astraea_pixel_port.sv ../astraea_draw.sv \
    ../astraea_copper.sv ../astraea_chip.sv \
    ../vega_sprite_builder.sv ../vega_video.sv \
    ../uart_tx.sv ../uart_rx.sv ../uart_rx_fifo.sv ../spi_sd.sv \
    ../astra_host_async_byte_fifo.sv ../astra_host_spi_slave.sv \
    ../astra_async_fifo.sv ../astra_host_runtime.sv \
    ../astra_host_service.sv boot_core.v
./obj_dir_boot/Vtb_boot
