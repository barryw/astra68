#!/usr/bin/env bash
# usage: table.sh OUTDIR...  per-entity ALUTs / registers / block bits
for o in "$@"; do
  echo "== $o $(grep 'Logic utilization' "$o/ooc.syn.summary")"
  n=$(grep -n '; Partition "root_partition" Resource Utilization by Entity' "$o/ooc.syn.rpt" | tail -1 | cut -d: -f1)
  sed -n "$((n+5)),$((n+60))p" "$o/ooc.syn.rpt" | grep -E '^;\s+\|(dut|blitter_i|fast_copy_i|flood_i|geometry_i|glyph_i|pixel_writer_i|surface_validator_i|texture_i)\|' | awk -F';' '{printf "%-28s %-22s %-22s %s\n", $2, $3, $4, $5}'
done
