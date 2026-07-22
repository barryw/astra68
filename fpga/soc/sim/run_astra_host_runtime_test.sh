#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

rm -rf build/astra_host_runtime
mkdir -p build/astra_host_runtime

iverilog -g2012 -s tb_astra_host_runtime \
    -o build/astra_host_runtime/iverilog \
    ../astra_async_fifo.sv ../astra_host_runtime.sv \
    tb_astra_host_runtime.sv
vvp build/astra_host_runtime/iverilog

verilator --binary --timing -Wno-fatal -Wno-lint \
    --Mdir build/astra_host_runtime/verilator \
    --top-module tb_astra_host_runtime \
    ../astra_async_fifo.sv ../astra_host_runtime.sv \
    tb_astra_host_runtime.sv
build/astra_host_runtime/verilator/Vtb_astra_host_runtime
