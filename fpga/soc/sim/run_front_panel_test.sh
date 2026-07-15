#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

rm -rf build/front_panel
mkdir -p build/front_panel

iverilog -g2012 -s tb_astra_front_panel \
    -o build/front_panel/iverilog \
    tb_astra_front_panel.sv ../astra_front_panel.sv
vvp build/front_panel/iverilog

verilator --binary --timing -Wall -Wno-fatal -Wno-UNUSEDSIGNAL -Wno-BLKSEQ \
    --Mdir build/front_panel/verilator \
    --top-module tb_astra_front_panel \
    tb_astra_front_panel.sv ../astra_front_panel.sv
build/front_panel/verilator/Vtb_astra_front_panel
