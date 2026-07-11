#!/bin/bash
# Build and run the coretest ROM in the SoC netlist simulation.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/../.."
source ~/oss-cad-suite/environment 2>/dev/null || true

python3 fpga/cpu/wf68k30L/check_exception_reset.py
python3 fpga/cpu/wf68k30L/check_opcode_pipe_reset.py

make -C sw/coretest clean all EXTRA_CFLAGS=-DCORETEST_SIM_IRQ
make -C sw/coretest check-disasm EXTRA_CFLAGS=-DCORETEST_SIM_IRQ

cd fpga/soc/sim
rm -rf obj_dir wf68k_core.v rom_init.hex
cp ../../../sw/coretest/rom_coretest.hex rom_init.hex
bash mkcore.sh
verilator --binary -j 0 --top-module tb_coretest -Wno-lint --timing -DASTRA_SOC_SIM_IRQ \
    tb_coretest.sv ../astra_soc.sv ../uart_tx.sv ../uart_rx.sv wf68k_core.v
./obj_dir/Vtb_coretest
