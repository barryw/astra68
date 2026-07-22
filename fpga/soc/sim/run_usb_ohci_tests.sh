#!/bin/bash
set -euo pipefail

sim_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
soc_dir="$(cd "$sim_dir/.." && pwd)"
build_dir="$sim_dir/build/usb_ohci"
mkdir -p "$build_dir"

iverilog -g2012 -o "$build_dir/ctrl_cdc" \
    "$soc_dir/usb_ohci_ctrl_cdc.sv" \
    "$sim_dir/tb_usb_ohci_ctrl_cdc.sv"
vvp "$build_dir/ctrl_cdc"

iverilog -g2012 -o "$build_dir/dma_bridge" \
    "$soc_dir/astra_async_fifo.sv" \
    "$soc_dir/usb_ohci_dma_bridge.sv" \
    "$sim_dir/tb_usb_ohci_dma_bridge.sv"
vvp "$build_dir/dma_bridge"

iverilog -g2012 -o "$build_dir/host" \
    "$soc_dir/astra_async_fifo.sv" \
    "$soc_dir/usb_ohci_ctrl_cdc.sv" \
    "$soc_dir/usb_ohci_dma_bridge.sv" \
    "$soc_dir/thirdparty/usb_ohci/UsbOhciWishbone_Dw32_Pc1_Pf48000000.v" \
    "$soc_dir/usb_ohci_host.sv" \
    "$sim_dir/tb_usb_ohci_host.sv"
vvp "$build_dir/host"
