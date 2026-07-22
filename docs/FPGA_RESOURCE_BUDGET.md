# FPGA resource budget

This document records Astra 68's production capacity on the ULX3S LFE5U-85F.
The current release has no artificial utilization cap: physical device
capacity and a successful timing-clean route are the limits.

The executable limits live in
`fpga/soc/oss_flow/resource_budgets.json`. Every routed `mkbit.sh` build checks
its nextpnr JSON report with `check_resource_budget.py` before packing a
bitstream.

## Limits

| Gate | TRELLIS_COMB | DP16KD | MULT18X18D | Purpose |
|---|---:|---:|---:|---|
| Device | 83,640 | 208 | 156 | Physical LFE5U-85F capacity |
| Retired planning guide | 62,730 (75%) | 156 (75%) | 117 (75%) | Historical estimate, not a release gate |
| Production maximum | 83,640 (100%) | 208 (100%) | 156 (100%) | Physical device capacity |

`kernel_platform_v1` is the canonical executable profile. It permits the full
device but does not waive timing, SCC, clock-domain, or hardware acceptance
checks. The older 65% and 75% profiles remain useful for historical comparison.

## Routed baseline

The current guarded P54 deterministic-POR 60 MHz design uses the MC68030/PMMU,
AstraHost boot and runtime storage/input service, OHCI USB, Vesta IRQ/timers,
SDRAM, HDMI, Astraea, and tile-free Vega feature set. Canonical Beast Yosys
`-abc2` mapping for build ID `0x60000002` reports 53,190 LUT4s, 25,420
synthesized FF cells, 104 block RAMs, and 19 multipliers. nextpnr packs:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 66,566 (79.59%) | 17,074 |
| TRELLIS_FF | 25,453 (30.43%) | 58,187 |
| DP16KD | 104 (50.00%) | 104 |
| MULT18X18D | 19 (12.18%) | 137 |

This baseline includes the integrated MC68030 PMMU, external line caches,
SDRAM and boot paths, HDMI, POST, front panel, Vesta IRQ/timers, AstraHost
runtime block/input transport, complete Astraea drawing and copper engines,
Vega framebuffer/scroll/sprite scanout, OHCI USB, and their integration logic.
It does not include Lyra audio or future math hardware.

The legal P54 router1 checkpoint passes all resource and protected-LUT gates,
routes every production clock at 14.544609 MHz CPU and 66.409882 MHz SDRAM or
better, passes its BRAM-only route probe, and boots through complete POST and
kernel entry repeatedly on ULX3S. The previous corrected-GSR mapping packed
66,765 combinational sites but its route probe was silent and it remains
rejected. Resource improvement alone is not an acceptance result; the current
baseline is retained because physical and board behavior also pass.

The 17,074 free combinational sites are nominal capacity, not a promise that a
later block will route. Every added block still needs isolated and integrated
measurements. Congestion can become the practical limit before the device is
numerically full.

## P55 routed checkpoint

P55 removes the hardware-proven P54 route's worst AstraHost ownership cone.
Canonical Beast synthesis for committed build `E9FB3E20` reports 52,615 LUT4s,
25,421 total mapped FFs, 5,075 CCU2Cs, 104 block RAMs, and 19 multipliers. That
is 575 fewer LUT4s than P54 with the same production feature set. The exact
strict route packs:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 66,095 (79.03%) | 17,545 |
| TRELLIS_FF | 25,450 (30.43%) | 58,190 |
| DP16KD | 104 (50.00%) | 104 |
| MULT18X18D | 19 (12.18%) | 137 |

It passes every production clock at 14.09 MHz CPU and 64.02 MHz SDRAM or
better, passes the protected-LUT gate, and repeatedly reaches full POST and
kernel entry on hardware. P55 is not yet the release baseline because the
normal POST font reads effective bank 3 on its Y46 BRAM placement.

The exact-depth source correction synthesizes the complete design to 52,565
LUT4s, 25,420 mapped FFs, 5,099 CCU2Cs, 101 block RAMs, and 19 multipliers.
The font is now one 2048x9 `DP16KD`, so the three unused font-bank blocks are
physically absent rather than counted as prospective savings. This remains a
synthesis-only checkpoint; routed combinational usage and final physical
headroom are unchanged until the corrected exact route completes.

## Acceptance rules

1. Architectural behavior and correctness tests pass before area is accepted.
2. Every new engine has isolated and integrated packed-resource measurements.
3. Memories use DP16KD where their access contract permits it; LUT RAM requires
   a measured reason.
4. A block that exceeds its allowance is restructured, serialized, or moved to
   software/ESP only when that preserves the intended architecture and
   throughput. Features are not silently removed to make a number pass.
5. The integrated image must route with zero combinational SCCs and meet every
   production clock. Nominal packing headroom does not compensate for
   congestion or a failed clock domain; independent seeds provide additional
   confidence but are not a prerequisite for first hardware bring-up.
6. Resource reports, timing reports, test provenance, and the exact source
   revision ship together. A successful synthesis alone is not a release gate.
