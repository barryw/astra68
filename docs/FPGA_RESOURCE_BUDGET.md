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

## Routed release baseline

The current committed `B1F9E60D` 60 MHz release uses the MC68030/PMMU,
AstraHost boot and runtime storage/input service, OHCI USB, Vesta IRQ/timers,
SDRAM, HDMI, Astraea, and tile-free Vega feature set. Canonical Beast Yosys
`-abc2` mapping reports 52,565 LUT4s, 25,420 synthesized FF cells, 101 block
RAMs, and 19 multipliers. The strict router1 result packs:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 66,093 (79.02%) | 17,547 |
| TRELLIS_FF | 25,449 (30.43%) | 58,191 |
| DP16KD | 101 (48.56%) | 107 |
| MULT18X18D | 19 (12.18%) | 137 |

This baseline includes the integrated MC68030 PMMU, external line caches,
SDRAM and boot paths, HDMI, POST, front panel, Vesta IRQ/timers, AstraHost
runtime block/input transport, complete Astraea drawing and copper engines,
Vega framebuffer/scroll/sprite scanout, OHCI USB, and their integration logic.
It does not include Lyra audio or future math hardware.

The exact route passes all resource, font-ROM, protected-LUT, SCC, and clock
gates. It meets the locked constraints at 13.646847 MHz CPU and 65.789474 MHz
SDRAM or better, and three consecutive FPGA-only reloads pass exact identity,
complete POST, full-range SDRAM BIST, and kernel entry on ULX3S. Bitstream
SHA-256 is
`05b9e84d2413c9390163a38f77c4d8ad08600a6adb619e69ebb25c56ae0e4eae`.

The 17,547 free combinational sites are nominal capacity, not a promise that a
later block will route. Every added block still needs isolated and integrated
measurements. Congestion can become the practical limit before the device is
numerically full.

## Multi-row blitter correction candidate

The 2026-07-22 candidate replaces Astraea's hardware-failing combinational
16x16 range-validation multiplier with a deterministic 16-step unsigned
shift/add path. Canonical Beast synthesis of the complete production feature
set reports 52,728 LUT4s, 25,492 FFs, 101 block RAMs, and 18 multipliers, with
zero SCCs and GSR enabled on all FFs. Relative to `B1F9E60D`, this is +163
LUT4s, +72 mapped FFs, unchanged block RAM, and -1 multiplier. The accepted
routed baseline contains 25,449 packed FFs, so synthesized and packed FF deltas
must not be compared directly.

This is a synthesis checkpoint, not capacity or release evidence. Packed
TRELLIS_COMB usage and physical headroom remain unknown until the exact
full-feature route completes and passes every constrained clock.

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
physically absent rather than counted as prospective savings. The corrected
`B1F9E60D` route is the release baseline above; P55 remains only the historical
checkpoint that isolated the font-bank problem.

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
