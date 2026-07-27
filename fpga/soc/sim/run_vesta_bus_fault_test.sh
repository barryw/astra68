#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

rm -rf build/vesta_bus_fault
mkdir -p build/vesta_bus_fault

iverilog -g2012 -s tb_vesta_bus_fault \
    -o build/vesta_bus_fault/iverilog \
    tb_vesta_bus_fault.sv ../vesta_bus_fault.sv
vvp build/vesta_bus_fault/iverilog

verilator --binary --timing -Wall -Wno-fatal -Wno-UNUSEDSIGNAL \
    --Mdir build/vesta_bus_fault/verilator \
    --top-module tb_vesta_bus_fault \
    tb_vesta_bus_fault.sv ../vesta_bus_fault.sv
build/vesta_bus_fault/verilator/Vtb_vesta_bus_fault
