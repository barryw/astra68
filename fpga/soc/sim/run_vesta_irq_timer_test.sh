#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

rm -rf build/vesta_irq_timer
mkdir -p build/vesta_irq_timer

iverilog -g2012 -s tb_vesta_irq_timer \
    -o build/vesta_irq_timer/iverilog \
    tb_vesta_irq_timer.sv ../vesta_irq_timer.sv
vvp build/vesta_irq_timer/iverilog

verilator --binary --timing -Wall -Wno-fatal -Wno-UNUSEDSIGNAL \
    --Mdir build/vesta_irq_timer/verilator \
    --top-module tb_vesta_irq_timer \
    tb_vesta_irq_timer.sv ../vesta_irq_timer.sv
build/vesta_irq_timer/verilator/Vtb_vesta_irq_timer
