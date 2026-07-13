#!/bin/bash
# Build and run the coretest ROM in the SoC netlist simulation.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/../.."
source ~/oss-cad-suite/environment 2>/dev/null || true

CPU_CORE="${CPU_CORE:-wf68k}"
verilator_cpu_args=()
verilator_debug_args=()
verilator_sdram_args=()
verilator_sdram_sources=()
coretest_cpu_cflags=()
case "$CPU_CORE" in
    wf68k|wf68k30l)
        CPU_CORE="wf68k"
        python3 fpga/cpu/wf68k30L/check_exception_reset.py
        python3 fpga/cpu/wf68k30L/check_opcode_pipe_reset.py
        ;;
    tg68k|tg68k_c)
        CPU_CORE="tg68k"
        verilator_cpu_args=(-GCPU_TG68K=1)
        ;;
    tg68k030|tg68k_mmu|tg68k_c_030_mmu)
        CPU_CORE="tg68k030"
        verilator_cpu_args=(-GCPU_TG68K=1)
        coretest_cpu_cflags=(-m68030 -DCORETEST_CPU_TG68K030=1)
        ;;
    *)
        echo "unknown CPU_CORE='$CPU_CORE' (expected wf68k, tg68k, or tg68k030)" >&2
        exit 2
        ;;
esac
if [[ "${CORETEST_DEBUG_IRQ:-0}" == "1" ]]; then
    verilator_debug_args+=(-GDEBUG_IRQ_PARAM=1)
fi
if [[ "${CORETEST_DEBUG_PMMU:-0}" == "1" ]]; then
    verilator_debug_args+=(-GDEBUG_PMMU_PARAM=1)
fi
if [[ "${CORETEST_DEBUG_CHK:-0}" == "1" ]]; then
    verilator_debug_args+=(-GDEBUG_CHK_PARAM=1)
fi
if [[ "${CORETEST_SIM_PROGRESS:-0}" == "1" ]]; then
    coretest_cpu_cflags+=(-DCORETEST_SIM_PROGRESS=1)
fi
if [[ "${CORETEST_TRAP_EXCEPTION_MARKERS:-0}" == "1" ]]; then
    verilator_debug_args+=(-GTRAP_EXCEPTION_MARKERS_PARAM=1)
fi
if [[ -n "${CORETEST_SIM_TIMEOUT_PS:-}" ]]; then
    verilator_debug_args+=(-GSIM_TIMEOUT_PS_PARAM="$CORETEST_SIM_TIMEOUT_PS")
fi
if [[ "${CORETEST_SDRAM_BERR:-0}" == "1" ]]; then
    if [[ "$CPU_CORE" != "tg68k030" ]]; then
        echo "CORETEST_SDRAM_BERR requires CPU_CORE=tg68k030" >&2
        exit 2
    fi
    coretest_cpu_cflags+=(-DCORETEST_SIM_FOCUS_SDRAM_BERR=1)
    verilator_sdram_args+=(-GSDRAM_ENABLE_PARAM=1)
    verilator_sdram_sources=(
        ecp5pll_sim.sv
        ../astraea_blitter.sv
        ../sdram32_controller.sv
        ../sdram32_cpu_bridge.sv
        ../sdram32_bist.sv
        ../thirdparty/core_sdram_axi4/sdram_axi_core.v
    )
fi
# TG68K currently converts BKPT directly to an illegal-instruction exception;
# unlike WF68K30L, it does not emit the optional CPU-space acknowledge cycle.
if [[ "$CPU_CORE" != "wf68k" || "${CORETEST_EXTRA_CFLAGS:-}" == *CORETEST_SIM_FOCUS* ]]; then
    verilator_debug_args+=(-GEXPECT_BKPT_ACK_PARAM=0)
fi
echo "coretest CPU_CORE=$CPU_CORE"
sim_cflags="-DCORETEST_SIM_IRQ ${coretest_cpu_cflags[*]:-} ${CORETEST_EXTRA_CFLAGS:-}"

make -C sw/coretest clean all EXTRA_CFLAGS="$sim_cflags"
if [[ "$sim_cflags" != *CORETEST_SIM_FOCUS* ]]; then
    make -C sw/coretest check-disasm EXTRA_CFLAGS="$sim_cflags"
fi

cd fpga/soc/sim
if [[ "${CORETEST_REUSE_SIM:-0}" != "1" || ! -x obj_dir/Vtb_coretest ]]; then
    rm -rf obj_dir wf68k_core.v
    bash mkcore.sh
    verilator --binary -j 0 --top-module tb_coretest -Wno-lint -Wno-UNOPTFLAT --timing -DASTRA_SOC_SIM_IRQ \
        "${verilator_cpu_args[@]}" \
        "${verilator_debug_args[@]}" \
        "${verilator_sdram_args[@]}" \
        tb_coretest.sv tb_sdram32_controller.sv ../astra_soc.sv \
        ../tg68k_cache_store.sv \
        ../uart_tx.sv ../uart_rx.sv "${verilator_sdram_sources[@]}" wf68k_core.v
fi
cp ../../../sw/coretest/rom_coretest.hex rom_init.hex
./obj_dir/Vtb_coretest
