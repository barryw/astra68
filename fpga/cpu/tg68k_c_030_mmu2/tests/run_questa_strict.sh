#!/usr/bin/env bash

set -u
set -o pipefail

usage() {
  echo "usage: $0 MODELSIM_ROOT OUT_DIR" >&2
  exit 2
}

[[ $# -eq 2 ]] || usage

modelsim_root=$1
out_dir=$2
script_dir=$(cd "$(dirname "$0")" && pwd)
core_dir=$(cd "$script_dir/.." && pwd)
test_dir="$script_dir/upstream"
bench_filter=${BENCH_FILTER:-}

vlib="$modelsim_root/linuxaloem/vlib"
vcom="$modelsim_root/linuxaloem/vcom"
vsim="$modelsim_root/linuxaloem/vsim"

for tool in "$vlib" "$vcom" "$vsim"; do
  if [[ ! -x "$tool" ]]; then
    echo "missing Questa tool: $tool" >&2
    exit 2
  fi
done

if grep -Eq '00DD4000|MiSTer shared-memory trapdoor' "$core_dir/TG68KdotC_Kernel.vhd"; then
  echo "generic CPU contains the MiSTer PMMU bypass" >&2
  exit 2
fi

mkdir -p "$out_dir/logs"
out_dir=$(cd "$out_dir" && pwd)
cd "$out_dir" || exit 2

base_log="$out_dir/logs/base_compile.log"
: >"$base_log"
"$vlib" work >>"$base_log" 2>&1 || exit 2

base_sources=(
  TG68K_Pack.vhd
  TG68K_ALU.vhd
  TG68K_PMMU_030.vhd
  TG68K_Cache_030.vhd
  TG68K_CacheCtrl_030.vhd
  TG68KdotC_Kernel.vhd
  TG68K.vhd
)

for source in "${base_sources[@]}"; do
  if ! "$vcom" -2008 "$core_dir/$source" >>"$base_log" 2>&1; then
    echo "BASE_COMPILE_FAIL $source"
    exit 2
  fi
done

if [[ ! -e data ]]; then
  ln -s "$test_dir/data" data
fi

summary="$out_dir/summary.txt"
: >"$summary"
compile_failures=0
simulation_failures=0
unscored=0
clean=0
matched=0

bench_sources=(
  "$test_dir"/tb_*.vhd
  "$test_dir"/mock/tb_*.vhd
  "$script_dir"/motorola/tb_*.vhd
)
for source in "${bench_sources[@]}"; do
  bench=$(basename "$source" .vhd)
  if [[ -n "$bench_filter" && ! "$bench" =~ $bench_filter ]]; then
    continue
  fi
  matched=$((matched + 1))
  compile_log="$out_dir/logs/$bench.compile.log"

  if ! "$vcom" -2008 "$source" >"$compile_log" 2>&1; then
    echo "COMPILE_FAIL $bench" | tee -a "$summary"
    compile_failures=$((compile_failures + 1))
    continue
  fi

  duration=1ms
  case "$bench" in
    tb_basic_*) duration=10ms ;;
    tb_pload_all_modes) duration=2ms ;;
  esac

  variants=("default:")
  if [[ "$bench" == tb_basic_cputest_exact ]]; then
    variants=(
      "suite0:-gsuite_select=0"
      "suite1:-gsuite_select=1"
      "suite2:-gsuite_select=2"
      "suite3:-gsuite_select=3"
      "suite7:-gsuite_select=7"
    )
  fi

  for variant in "${variants[@]}"; do
    label=${variant%%:*}
    generic=${variant#*:}
    bench_label=$bench
    if [[ "$label" != default ]]; then
      bench_label="${bench}_${label}"
    fi
    sim_log="$out_dir/logs/$bench_label.sim.log"

    timeout 900 "$vsim" -c $generic \
      -do "set StdArithNoWarnings 1; set NumericStdNoWarnings 1; run $duration; quit -f" \
      "work.$bench" >"$sim_log" 2>&1
    sim_rc=$?

    if [[ $sim_rc -eq 124 ]]; then
      echo "TIMEOUT $bench_label" | tee -a "$summary"
      simulation_failures=$((simulation_failures + 1))
    elif [[ $sim_rc -ne 0 ]]; then
      echo "SIM_EXIT_FAIL $bench_label rc=$sim_rc" | tee -a "$summary"
      simulation_failures=$((simulation_failures + 1))
    elif grep -Eq '^# \*\* (Error|Fatal):|FAIL:|TEST FAILED|SOME TESTS FAILED|OVERALL: SOME|RESULT: [1-9][0-9]* failed' "$sim_log"; then
      echo "SIM_FAIL $bench_label rc=$sim_rc" | tee -a "$summary"
      simulation_failures=$((simulation_failures + 1))
    elif grep -E '^# \*\* Failure:' "$sim_log" | grep -Eiv 'Simulation (complete|finished|end)|End of simulation' >/dev/null; then
      echo "SIM_FAILURE_ASSERT $bench_label rc=$sim_rc" | tee -a "$summary"
      simulation_failures=$((simulation_failures + 1))
    elif grep -Eiq 'PASS|passed|complete|result:' "$sim_log"; then
      echo "RAN_CLEAN $bench_label rc=$sim_rc" | tee -a "$summary"
      clean=$((clean + 1))
    else
      echo "RAN_UNSCORED $bench_label rc=$sim_rc" | tee -a "$summary"
      unscored=$((unscored + 1))
    fi
  done
done

if [[ $matched -eq 0 ]]; then
  echo "NO_BENCH_MATCH BENCH_FILTER=$bench_filter" | tee -a "$summary"
  exit 2
fi

total=$((compile_failures + simulation_failures + unscored + clean))
result="STRICT_RESULT total=$total clean=$clean compile_failures=$compile_failures simulation_failures=$simulation_failures unscored=$unscored"
echo "$result" | tee -a "$summary"

failures=$((compile_failures + simulation_failures + unscored))
if [[ $failures -gt 125 ]]; then
  failures=125
fi
exit "$failures"
