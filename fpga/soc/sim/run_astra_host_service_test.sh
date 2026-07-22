#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

rm -rf build/astra_host_service
mkdir -p build/astra_host_service

iverilog -g2012 -s tb_astra_host_service \
    -o build/astra_host_service/iverilog \
    ../astra_host_service.sv tb_astra_host_service.sv
vvp build/astra_host_service/iverilog

verilator --binary --timing -Wno-fatal --top-module tb_astra_host_service \
    --Mdir build/astra_host_service/verilator \
    ../astra_host_service.sv tb_astra_host_service.sv
build/astra_host_service/verilator/Vtb_astra_host_service
