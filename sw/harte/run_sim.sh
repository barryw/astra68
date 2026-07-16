#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
source ~/oss-cad-suite/environment 2>/dev/null || true

make -C "$ROOT/sw/harte" clean all

cd "$ROOT/fpga/soc/sim"
cp ../../../sw/harte/rom_harness.hex rom_init.hex
CORE_OUT=harte_core.v bash mkcore.sh

verilator --binary -j 0 --Mdir obj_dir_harte --top-module tb_soc \
    -Wno-lint -Wno-UNOPTFLAT --timing \
    tb_soc.sv ../astra_soc.sv ../astra_front_panel.sv ../boot_memory_map.sv \
    ../astraea_blitter.sv ../astraea_pixel_port.sv ../astraea_draw.sv \
    ../astraea_copper.sv ../astraea_chip.sv \
    ../tg68k_cache_store.sv ../vega_tile_builder.sv ../vega_sprite_builder.sv \
    ../vega_video.sv \
    ../uart_tx.sv ../uart_rx.sv ../uart_rx_fifo.sv ../spi_sd.sv \
    ../astra_host_async_byte_fifo.sv ../astra_host_spi_slave.sv \
    ../astra_host_boot.sv harte_core.v

./obj_dir_harte/Vtb_soc
