# FPGA resource budget

This document defines Astra 68's production capacity policy for the ULX3S
LFE5U-85F. Free fabric is a product requirement: the currently specified
chipset may not consume the capacity reserved for hardware that has not been
designed yet.

The executable limits live in
`fpga/soc/oss_flow/resource_budgets.json`. Every routed `mkbit.sh` build checks
its nextpnr JSON report with `check_resource_budget.py` before packing a
bitstream.

## Limits

| Gate | TRELLIS_COMB | DP16KD | MULT18X18D | Purpose |
|---|---:|---:|---:|---|
| Device | 83,640 | 208 | 156 | Physical LFE5U-85F capacity |
| Core + graphics | 54,366 (65%) | 104 (50%) | 78 (50%) | Current 68030/PMMU, platform, Astraea, and Vega image |
| Complete specified chipset | 62,730 (75%) | 156 (75%) | 117 (75%) | Vesta, Astraea, Vega, and Lyra target |
| Absolute stop | 66,912 (80%) | 166 (80%) | 124 (79%) | Cannot be waived by a build option |

The complete-chipset target leaves 20,910 packed logic sites, 52 block RAMs,
and 39 multipliers for later hardware. That 25% reserve is not an allowance for
unfinished Vesta, Astraea, Vega, or Lyra work; those chips must fit below the
complete-chipset target. The absolute stop still leaves 16,728 logic sites,
but it is emergency routing contingency, not capacity available to a feature.

Advancing the canonical build from one profile to another requires a reviewed
change to the checked-in `default_profile`. A command-line environment variable
cannot select a larger envelope.

## Current baseline

The exact July 15, 2026 core-plus-graphics checkpoints synthesized with the
canonical Yosys `-abc2` mapping currently bracket the timing work:

| Checkpoint | TRELLIS_COMB | Profile margin | Status |
|---|---:|---:|---|
| P39 | 54,003 | 363 | Last checkpoint inside the 65% profile; best SDRAM route is 68.65 MHz. |
| P41 | 54,439 | -73 | Registered 15-label blitter phase boundary; rejected on area and timing. |
| P44 | 54,399 | -33 | Eleven-fact boundary; exact tests pass, but all routes and area fail. |
| P45 | 54,345 | 21 | Registered row-offset multiplier operand; exact tests pass, first route reaches 71.74 MHz SDRAM and fails timing. |
| P46 | 54,191 | 175 | Removes the redundant request-count comparator; exact tests pass and full routes are active. |

P46's packed resources are:

| Resource | Used | Physical free | Current-profile margin |
|---|---:|---:|---:|
| TRELLIS_COMB | 54,191 (64.79%) | 29,449 | 175 |
| TRELLIS_FF | 18,282 (21.86%) | 65,358 | not limiting |
| DP16KD | 80 (38.46%) | 128 | 24 |
| MULT18X18D | 17 (10.90%) | 139 | 61 |
| TRELLIS_RAMW | 168 (1.61%) | 10,287 | not limiting |

This baseline includes the integrated MC68030 PMMU, external line caches,
SDRAM and boot paths, HDMI, POST, front panel, complete Astraea drawing and
copper engines, Vega framebuffer/tile/sprite scanout, and their integration
logic. It does not include the complete Vesta services or Lyra audio.

If P46 is retained, the complete-chipset envelope permits at most 8,539
additional packed logic sites. Its 175-cell current-profile margin is a gate
pass, not useful growth room. Even P39 leaves only 8,727 sites to the 75% target. The
existing planning allocation therefore exposes a real capacity problem:

| Remaining work | Initial allowance |
|---|---:|
| Complete Vesta IRQ, timers, input, and configuration service glue | 1,500 |
| Lyra PCM/wavetable voices, time-multiplexed mixer, and output | 4,500 |
| Shared DMA queues, CDC, arbitration, and integration | 1,500 |
| Measured-growth contingency at P46 | 1,039 |

These are admission limits, not estimates to spend. A 1,039-cell contingency is
not healthy evidence that the remaining chipset fits. Recover core/graphics
area and obtain a timing-clean route before admitting Lyra or the remaining
Vesta work. Each later block needs isolated and integrated synthesis
measurements before its allowance becomes credible. Unused allowance returns
to the future-hardware reserve.

The nominal 20,910-site reserve is not assumed to be completely routable.
Every later hardware block still needs isolated and integrated routes, and a
later block cannot justify pushing the completed base chipset above 75%.

## Acceptance rules

1. Architectural behavior and correctness tests pass before area is accepted.
2. Every new engine has isolated and integrated packed-resource measurements.
3. Memories use DP16KD where their access contract permits it; LUT RAM requires
   a measured reason.
4. A block that exceeds its allowance is restructured, serialized, or moved to
   software/ESP only when that preserves the intended architecture and
   throughput. Features are not silently removed to make a number pass.
5. The integrated image must route with zero combinational SCCs and meet every
   production clock across multiple retained seeds. Nominal packing headroom
   does not compensate for congestion or a failed clock domain.
6. Resource reports, timing reports, test provenance, and the exact source
   revision ship together. A successful synthesis alone is not a release gate.
