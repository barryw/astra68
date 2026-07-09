#!/bin/bash
# Build and run the coretest ROM in the SoC netlist simulation.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
source ~/oss-cad-suite/environment 2>/dev/null || true

make -C sw/coretest clean all
make -C sw/coretest check-disasm

cd fpga/soc/sim
cp ../../../sw/coretest/rom_coretest.hex rom_init.hex
bash mkcore.sh
verilator --binary -j 0 --top-module tb_coretest -Wno-lint --timing \
    tb_coretest.sv ../astra_soc.sv ../uart_tx.sv ../uart_rx.sv wf68k_core.v
./obj_dir/Vtb_coretest
