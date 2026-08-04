#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
xsa=${ASTRA_ARTY_XSA:-"$repo_root/build/arty-graphics/astra_arty_graphics.xsa"}
out_dir=${ASTRA_ARTY_FSBL_OUT:-"$repo_root/build/arty-graphics/fsbl"}
xsct=${XSCT:-/tools/Xilinx/Vitis/2024.2/bin/xsct}
tcl=${ASTRA_ARTY_FSBL_TCL:-"$repo_root/fpga/arty/scripts/build_fsbl.tcl"}
fclk1_hz=${ASTRA_ARTY_FCLK1_HZ:-200000000}

case "$fclk1_hz" in
    200000000)
        fclk1_maskwrite=0x00100500U
        ;;
    166666672)
        fclk1_maskwrite=0x00100600U
        ;;
    *)
        echo "unsupported FCLK1 rate: $fclk1_hz" >&2
        exit 3
        ;;
esac

test -x "$xsct"
test -f "$tcl"
test -s "$xsa"
if [[ -e "$out_dir" ]]; then
    echo "FSBL output already exists: $out_dir" >&2
    exit 2
fi

"$xsct" "$tcl" "$xsa" "$out_dir"

elf="$out_dir/executable.elf"
ps7_init="$out_dir/ps7_init.c"
test -s "$elf"
test -s "$ps7_init"
grep -Fq \
    'EMIT_MASKWRITE(0XF8000170, 0x03F03F30U ,0x00200500U)' \
    "$ps7_init"
grep -Fq \
    "EMIT_MASKWRITE(0XF8000180, 0x03F03F30U ,$fclk1_maskwrite)" \
    "$ps7_init"
file "$elf" | grep -Fq 'ELF 32-bit LSB executable, ARM'

cp "$elf" "$out_dir/zynq_fsbl.elf"
sha256sum "$xsa" "$out_dir/zynq_fsbl.elf" "$ps7_init" \
    >"$out_dir/SHA256SUMS"
printf '%s\n' \
    'FCLK0=100000000' \
    "FCLK1=$fclk1_hz" \
    >"$out_dir/CLOCKS"
echo "ASTRA_ARTY_FSBL PASS $out_dir/zynq_fsbl.elf"
