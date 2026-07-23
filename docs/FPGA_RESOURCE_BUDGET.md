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

The current committed `6C0D0CA3` 60 MHz release uses the MC68030/PMMU,
AstraHost boot and runtime storage/input service, OHCI USB, Vesta IRQ/timers,
SDRAM, HDMI, Astraea, and tile-free Vega feature set. Canonical Beast Yosys
`-abc2` mapping reports 52,728 LUT4s, 25,492 synthesized FF cells, 101 block
RAMs, and 18 multipliers. The strict router1 result packs:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 66,144 (79.08%) | 17,496 |
| TRELLIS_FF | 25,525 (30.52%) | 58,115 |
| DP16KD | 101 (48.56%) | 107 |
| MULT18X18D | 18 (11.54%) | 138 |

This baseline includes the integrated MC68030 PMMU, external line caches,
SDRAM and boot paths, HDMI, POST, front panel, Vesta IRQ/timers, AstraHost
runtime block/input transport, complete Astraea drawing and copper engines,
Vega framebuffer/scroll/sprite scanout, OHCI USB, and their integration logic.
It does not include Lyra audio or future math hardware.

The exact route passes all resource, font-ROM, protected-LUT, SCC, and clock
gates. It meets the locked constraints at 13.972139 MHz CPU and 63.403500 MHz
SDRAM or better. Route-preserving focused and complete graphics diagnostics,
four SRAM production boots, AstraHost restart/SPI recovery, physical HDMI,
persistent programming, and reset-from-flash POST/kernel entry all pass.
Bitstream SHA-256 is
`61538d09ef255b94206500185b31008fc242004ac954356365e0b9053c88e2d1`.

The 17,496 free combinational sites are nominal capacity, not a promise that a
later block will route. Every added block still needs isolated and integrated
measurements. Congestion can become the practical limit before the device is
numerically full.

## F4DC1E18 K1 candidate

Committed source `5798c5575a5bf6d5ca37eae2fbe63cb0528ad6e8` adds the retained
PMMU restart and translated exception-stack repairs without changing the
production feature set. Canonical Beast Yosys `-abc2` mapping reports 52,943
LUT4s, 25,522 synthesized FFs, 101 block RAMs, and 18 multipliers. The exact
strict seed-4 heap/router1 route packs:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 66,377 (79.36%) | 17,263 |
| TRELLIS_FF | 25,555 (30.55%) | 58,085 |
| DP16KD | 101 (48.56%) | 107 |
| MULT18X18D | 18 (11.54%) | 138 |

It passes every exact constraint at 14.015417 MHz CPU, 66.423111 MHz SDRAM,
72.432274 MHz USB, 55.673088 MHz pixel, and 307.125305 MHz HDMI shift. The
bitstream SHA-256 is
`bf6b86079227e042676ef495903162212a19092ab28fa83a7a09fbd261381d35`.
Three independent SRAM loads pass exact identity, full POST, 32 MiB BIST, DMA,
runtime/input initialization, and K0 kernel entry. A separate hardware-profile
coretest passes PMMU translation, invalid-descriptor recovery, and write
protection on the same exact routed image without changing these resource
counts. This remains a candidate rather than the routed release baseline until
the complete PMMU table-walk arbitration lock, physical HDMI check, and
reset-from-flash acceptance pass.

The follow-on K-HW3 source delta derives RMC from actual walker request/state
and closes the table-walk arbitration contract in simulation. It is not part of
the `F4DC1E18` mapped or routed counts above. Do not assign it a resource delta
until the complete production design is remapped; its route must still meet all
locked clocks and the unrestricted physical-capacity policy.

## 77B3CDC8 corrected K1 mapping

Exact corrected qualification snapshot
`77b3cdc8fddb984850073a2c2cb5998bbbe1d857` includes K-HW3, K-HW4, the K1
kernel, and the Motorola-directed processor-reset/ATC correction. NUC Yosys
maps the complete production feature set to 53,073 LUT4s, 25,532 GSR-enabled
FFs, 101 block RAMs, and 18 multipliers with zero SCCs. Exact seed-4
critical-floorplan placement finishes normally with checksum `0x7c9a8594` and
packs 66,513 TRELLIS_COMB and 25,561 TRELLIS_FF cells.

This is 471 fewer mapped LUT4s and 477 fewer packed combinational cells than
the pre-fix `66D6094F` checkpoint; block RAM and multiplier use are unchanged.
The exact no-waiver strict route is active. These counts prove that reset
conformance did not consume the remaining fabric, but they are not a timing or
bitstream claim.

## B1F9E60D rollback comparison

The prior `B1F9E60D` route packed 66,093 TRELLIS_COMB cells, 25,449 FFs,
101 block RAMs, and 19 multipliers and reached 13.646847 MHz CPU and
65.789474 MHz SDRAM. It remains a known bootable rollback image but rejects
multi-row blits in hardware. The promoted shift/add correction uses 51 more
TRELLIS_COMB sites and 76 more packed FFs while removing one multiplier; block
RAM is unchanged. Synthesized and packed FF deltas must not be compared
directly.

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
