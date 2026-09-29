#!/usr/bin/env bash
# Quartus Pro out-of-context synthesis of the render engines (area only).
# usage: run_ooc_q.sh OUTDIR [TREE-ROOT]
set -euo pipefail
S=$(cd "$(dirname "$0")" && pwd)
OUT=$1; ROOT=${2:-$(cd "$S/../../../.." && pwd)}
G=$ROOT/fpga/arty/graphics
Q=${QUARTUS_ROOT:-/home/barry/altera_pro/26.1.1/quartus}/bin
export LM_LICENSE_FILE=${LM_LICENSE_FILE:-/home/barry/.altera.quartus/quartus2_lic.dat}
rm -rf "$OUT"; mkdir -p "$OUT"; cd "$OUT"
echo 'PROJECT_REVISION = "ooc"' > ooc.qpf
{
echo 'set_global_assignment -name FAMILY "Agilex 5"'
echo 'set_global_assignment -name DEVICE A5EB013BB23BE4SCS'
echo 'set_global_assignment -name TOP_LEVEL_ENTITY ooc_top'
echo "set_global_assignment -name SEARCH_PATH $G"
echo "set_global_assignment -name SYSTEMVERILOG_FILE $S/ooc_top.sv"
for f in surface_validator pixel_writer copy_burst blitter geometry flood glyph texture command_processor; do
  echo "set_global_assignment -name SYSTEMVERILOG_FILE $G/astra_render_$f.sv"
done
} > ooc.qsf
"$Q/quartus_syn" ooc -c ooc > syn.log 2>&1 || { tail -30 syn.log; exit 1; }
"$S/table.sh" "$OUT"
