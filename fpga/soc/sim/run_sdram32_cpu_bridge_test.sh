#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

rm -rf build/sdram32_cpu_bridge
mkdir -p build/sdram32_cpu_bridge

iverilog -g2012 -s tb_sdram32_cpu_bridge \
    -o build/sdram32_cpu_bridge/iverilog \
    tb_sdram32_cpu_bridge.sv ../sdram32_cpu_bridge.sv
vvp build/sdram32_cpu_bridge/iverilog

verilator --binary --timing -Wall -Wno-fatal -Wno-UNUSEDSIGNAL \
    --Mdir build/sdram32_cpu_bridge/verilator \
    --top-module tb_sdram32_cpu_bridge \
    tb_sdram32_cpu_bridge.sv ../sdram32_cpu_bridge.sv
build/sdram32_cpu_bridge/verilator/Vtb_sdram32_cpu_bridge
