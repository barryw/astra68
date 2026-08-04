# FPGA resource budget

## Active Arty Z7-20 budget (2026-07-30)

The active PL target is `xc7z020clg400-1` on the Arty Z7-20 attached to
`beast`. It provides 53,200 LUTs, 106,400 flip-flops, 140 36-Kbit block RAMs,
and 220 DSP slices. The MC68030/PMMU executes through QEMU on the ARM processing
system and consumes no PL LUT, BRAM, or DSP capacity.

There is no artificial utilization cap. A design is accepted only when the
complete production feature set fits, routes, meets every clock including the
74.25 MHz 720p pixel path and its 371.25 MHz HDMI serializer clock, and passes
the hardware gates in
[`GRAPHICS_ARCHITECTURE.md`](GRAPHICS_ARCHITECTURE.md). Isolated synthesis and
nominal free resources are planning evidence only.

The PS/DDR framebuffer, two-tile-layer scanout, and dynamic boot-text baseline
has a qualified full route, exact boot package, and hardware DDR readback
result. It uses 13,096 total LUTs, 12,892 registers, 29.5 BRAM36-equivalent
tiles, and five DSPs. The active complete-renderer release below supersedes that
baseline for current capacity planning. Linux reserves the version-1 128 MiB
graphics arena as `no-map`; that DDR allocation is not a PL resource count.

Version 1 now includes two INDEX4/INDEX8 tile layers. The retired 720-pixel
4bpp tile builder maps in an isolated XC7 planning run to approximately 1,220
logic cells, 1,331 flip-flops, four BRAM36 equivalents, and no DSPs. That is a
historical lower-bound data point. The provisional envelope for both 720p
tile layers, their shared AXI path, palette integration, and final composition
was 4,000 LUTs and 16 BRAM36s. It was neither a utilization cap nor release
evidence.

That provisional envelope is now superseded by the integrated checkpoint
below. Historical component values remain useful for attributing growth but
must not be added to the integrated total.

### Arty integrated scanout checkpoint

Exact Beast Vivado 2024.2 checkpoint `boot-text6` contains Zynq PS integration,
GP0 graphics control, three 64-bit HP DDR read paths, framebuffer and two tile
line builders, palette stores, compositor, line scheduler, complete HDMI
transport, and the four-row tear-free boot-text plane. It fully routes with
+0.002 ns setup slack, +0.019 ns hold slack, +0.538 ns pulse-width slack, no
timing failures, and all 23,261 routable nets complete. It uses:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 13,096 | 24.62% | 40,104 |
| LUT as logic | 11,253 | 21.15% | 41,947 |
| LUT as memory | 1,843 | 10.59% of LUT-RAM capacity | 15,557 |
| Slice registers | 12,892 | 12.12% | 93,508 |
| BRAM36-equivalent tiles | 29.5 | 21.07% | 110.5 |
| DSP48 | 5 | 2.27% | 215 |

These are historical complete-system planning numbers for the first
implemented scanout path. They do not include the later sprites, copper,
blitter/virtual sprites, geometry/fill, AFNT glyph expansion, or command
execution now present in the qualified release. The complete copper checkpoint
below supersedes them for current capacity planning.

Bitstream SHA-256 is
`869b0b4917135486376ab868f5599963dced75a2f8cfa76b2261fe01d0439cf4`.
The exact source and report identities, functional coverage, active boot
hashes, and hardware evidence are recorded in
`fpga/arty/graphics/TIMING_CLOSURE.md`.

### Arty integrated 64-sprite checkpoint

Exact Beast Vivado 2024.2 checkpoint `sprite64-cdc-full2` is the historical
hardware-qualified sprite checkpoint. It adds the complete 64-sprite engine and the
line-slot bundled-data CDC correction. It fully routes with +0.024 ns setup,
+0.034 ns hold, +0.538 ns pulse-width slack, no failing endpoints, and all
41,778 routable nets complete. It uses:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 21,954 | 41.27% | 31,246 |
| Slice registers | 23,003 | 21.62% | 83,397 |
| BRAM36-equivalent tiles | 85.5 | 61.07% | 54.5 |
| DSP48 | 51 | 23.18% | 169 |

The qualified sprite system therefore costs 8,858 LUTs, 10,111 registers, 56
BRAM36-equivalent tiles and 46 DSPs over `boot-text6`. Its INDEX8 shape pixels
reside in the reserved DDR graphics arena and do not consume BRAM; the 85.5
BRAM total covers descriptors, palettes, working lines, published lines and
the pre-existing scanout system. Hardware stress, complete off-screen clipping,
edge clipping and the visible 64-sprite grid pass with zero drops, overflow,
AXI errors or deadline errors. These are historical complete-system planning
numbers for the implemented scanout and sprite path.

Bitstream SHA-256 is
`4b1cea2a4c97b96c6fda0d04d883c16f118cec95e35249a171020ee4e33380b2`.
Exact source, report and checkpoint identities are in
`fpga/arty/graphics/TIMING_CLOSURE.md`.

### Arty basic-renderer rejected route

Exact Beast Vivado 2024.2 checkpoint `basic-blitter-route-1` adds the bounded
command/completion transport, descriptor validation, shared pixel writer, and
basic clipped fill and overlap-safe same-format copy. Functional simulation
passes and all 54,974 nets route, but the checkpoint is not a release because
the 200 MHz renderer domain fails setup at -7.828 ns. Its measured resources
are useful for capacity planning only:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 29,363 | 55.19% | 23,837 |
| LUT as logic | 24,233 | 45.55% | 28,967 |
| LUT as memory | 5,130 | 29.48% of LUT-RAM capacity | 12,270 |
| Slice registers | 30,565 | 28.73% | 75,835 |
| BRAM36-equivalent tiles | 85.5 | 61.07% | 54.5 |
| DSP48 | 65 | 29.55% | 155 |

Relative to the hardware-qualified sprite release, this candidate adds 7,409
LUTs, 7,562 registers and 14 DSPs with no BRAM growth. The result confirms that
the first renderer stage fits comfortably in physical resources; it does not
prove timing closure or final graphics capacity. Exact paths, source hashes,
reports, and the rejected-artifact disposition are in
`fpga/arty/graphics/TIMING_CLOSURE.md`.

The subsequent `full-route-2` checkpoint contains the timing-clean focused
blitter repair. It also routes every net, but remains rejected at -5.236 ns
because command-processor range validation and completion-result selection
form the new limiting cone. Its measured planning totals are:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 28,900 | 54.32% | 24,300 |
| LUT as logic | 23,770 | 44.68% | 29,430 |
| LUT as memory | 5,130 | 29.48% of LUT-RAM capacity | 12,270 |
| Slice registers | 31,146 | 29.27% | 75,254 |
| BRAM36-equivalent tiles | 85.5 | 61.07% | 54.5 |
| DSP48 | 65 | 29.55% | 155 |

Relative to the qualified sprite release, this exact candidate adds 6,946
LUTs, 8,143 registers and 14 DSPs with no BRAM growth. Physical capacity is
still not the Stage 1 blocker; the measured 200 MHz command-processor path is.

### Arty Stage 1 basic renderer qualified route

Checkpoint `path-boundary-3/full-route-9` is the hardware-qualified Stage 1
renderer. It includes the complete framebuffer/tile/sprite/boot-text path plus
bounded submission and completion rings, surface validation, timeout/reset,
the shared pixel writer, clipped fill, and overlap-safe same-format copy. The
exact Beast Vivado 2024.2 route meets 200 MHz render/build timing at +0.003 ns,
74.25 MHz pixel timing at +1.462 ns, hold at +0.013 ns, and pulse width at
+0.538 ns. All 55,816 routable nets complete with zero errors.

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 28,549 | 53.66% | 24,651 |
| LUT as logic | 23,525 | 44.22% | 29,675 |
| LUT as memory | 5,024 | 28.87% of LUT-RAM capacity | 12,376 |
| Slice registers | 33,087 | 31.10% | 73,313 |
| Occupied slices | 10,982 | 82.57% | 2,318 |
| BRAM36-equivalent tiles | 84.5 | 60.36% | 55.5 |
| DSP48E1 | 61 | 27.73% | 159 |

Relative to the qualified sprite release, Stage 1 adds 6,595 LUTs, 10,084
registers and 10 DSPs while using one fewer BRAM36-equivalent tile after final
packing. Although logical LUT, register, BRAM and DSP headroom remains broad,
only 2,318 physical slices remain unused. Future graphics stages must therefore
track placed slice occupancy and timing, not infer capacity from LUT count
alone. Complete blitter/virtual sprites, geometry, glyphs, and copper are not
included in these totals.

The historical complete-blitter OOC checkpoints were useful for attributing
growth but are not additive capacity numbers. The exact integrated route below
superseded them at that stage; the later complete copper-qualified checkpoint
is the current complete-system planning baseline.

The exact bitstream SHA-256 is
`fbfd7f80572dd9b0783e94d61cacda4388453083c8a8cae39ffc131628eef2aa`.
Route, source, release and hardware evidence is in
`fpga/arty/graphics/TIMING_CLOSURE.md` and
`docs/evidence/astra-arty-render-basic-hardware-20260801.log`.

### Arty complete-blitter qualified route

Checkpoint `full-route-24-checkpoint-49` is the historical complete-blitter
release. It retains framebuffer and tile scanout, boot text, all 64
sprites, command/completion transport, descriptor validation, timeout/reset,
the shared pixel writer, and the complete blitter. The exact Beast Vivado
2024.2 route meets 200 MHz render/build timing at +0.013 ns, 74.25 MHz pixel
timing at +2.620 ns, hold at +0.051 ns, and pulse width at +0.538 ns. All
59,647 routable nets complete with zero errors.

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 30,185 | 56.74% | 23,015 |
| LUT as logic | 25,161 | 47.30% | 28,039 |
| LUT as memory | 5,024 | 28.87% of LUT-RAM capacity | 12,376 |
| Slice registers | 36,050 | 33.88% | 70,350 |
| Occupied slices | 11,695 | 87.93% | 1,605 |
| BRAM36-equivalent tiles | 84.5 | 60.36% | 55.5 |
| DSP48E1 | 66 | 30.00% | 154 |

Relative to Stage 1, the complete blitter adds 1,636 LUTs, 2,963 registers,
five DSPs, and 713 occupied slices while BRAM use is unchanged. Logical LUT,
register, BRAM, and DSP headroom remains substantial, but only 1,605 physical
slices are currently unused. Virtual sprites, geometry and pattern/flood
operations, AFNT glyph expansion, and copper are not included. Each must be
evaluated by a new exact full route; nominal LUT headroom alone is not a fit
claim.

The exact bitstream SHA-256 is
`96c98a4dadb5703efcc93121b3d6c6226dc319c52e9054697de98f1e8cca17a0`.
Active `BOOT.BIN` is
`dfd34dd31bafd199889d7d2cc1f9f2682b72636b296e4f4b3a1964d4ef6acbaa`.
Ten consecutive complete-blitter hardware runs pass 29 fenced commands and
verify 1,196,651 pixels with zero backpressure or engine errors. Exact route,
source, release, and hardware identities are in
`fpga/arty/graphics/TIMING_CLOSURE.md`.

### Arty geometry qualified checkpoint

Checkpoint-44 `full-route-17-166m667` adds line, rectangle, circle, ellipse,
pattern-fill, and bounded flood engines to the complete renderer. It was the
qualified capacity authority before AFNT. The exact Beast Vivado 2024.2 route at
166,666,672 Hz meets setup at `+0.060 ns`, hold at `+0.016 ns`, and pulse width
at `+0.538 ns`, with zero failing endpoints. AFNT glyph expansion and copper
are not included.

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 32,207 | 60.54% | 20,993 |
| Slice registers | 39,098 | 36.75% | 67,302 |
| Occupied slices | 12,344 | 92.81% | 956 |
| BRAM36-equivalent tiles | 84.5 | 60.36% | 55.5 |
| DSP48E1 | 70 | 31.82% | 150 |

Relative to the hardware-qualified complete-blitter release, this checkpoint
adds 2,022 LUTs, 3,048 registers, four DSPs, and 649 occupied slices. Virtual
sprites remain bounded groups of existing blitter commands and add no second
descriptor engine. The remaining 956 physical slices are the operative
capacity risk for AFNT and dual-bank copper; logical LUT and BRAM headroom must
not be used as a substitute for exact placed-and-routed evidence.

The exact bitstream SHA-256 is
`b2599c5c3b00f312fc4a8b149944243c0885741f5df061f91d521009ce24472b`.
Ten consecutive hardware-certification runs pass every geometry primitive,
the exact 60-pixel flood topology, and explicit bounded workspace overflow
with zero backpressure.

### Arty AFNT qualified checkpoint

`full-route-3-interbeat-gap-166m667` is the active qualified capacity
authority. It adds shared-pipeline AFNT glyph expansion for MASK1, A4, A8,
INDEX4, and INDEX8. The exact Beast Vivado 2024.2 route at 166,666,672 Hz
meets setup at `+0.078 ns`, hold at `+0.015 ns`, and pulse width at
`+0.538 ns`; all 68,601 routable nets complete without error. Copper is not
included.

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 34,379 | 64.62% | 18,821 |
| LUT as memory | 5,025 | 28.88% of LUT-RAM capacity | 12,375 |
| Slice registers | 40,952 | 38.49% | 65,448 |
| Occupied slices | 12,674 | 95.29% | 626 |
| BRAM36-equivalent tiles | 84.5 | 60.36% | 55.5 |
| DSP48E1 | 80 | 36.36% | 140 |

Relative to the geometry checkpoint, AFNT adds 2,172 LUTs, 1,854 registers,
ten DSPs, and 330 occupied slices with no BRAM growth. The 5,025 memory LUTs
are 3,774 distributed-RAM LUTs and 1,251 SRL LUTs; AFNT and the render command
processor themselves infer no LUT RAM. The dominant attributable memory LUTs
are the sprite line builder and collision banks, scene store, tile line
builders, boot text, framebuffer support, and PS SmartConnect FIFOs.

Only 626 physical slices remain unused. That density, rather than aggregate
LUT, register, BRAM, or DSP availability, is the primary copper integration
risk. Copper's dual 4096-instruction banks must be explicit BRAM, and an exact
full route is required before any capacity claim. Twenty consecutive complete
AFNT hardware runs and five independent sprite runs pass. Exact artifacts and
log hashes are in `fpga/arty/graphics/TIMING_CLOSURE.md`.

### Arty complete copper-qualified checkpoint

The complete production graphics design now includes the BRAM-only dual-bank
4096-instruction copper, WAIT/SKIP, validated MOVE, IRQ, command dispatch, and
hardware register timing classes. The exact Beast Vivado 2024.2 route at
166,666,672 Hz meets setup at `+0.036 ns` and hold at `+0.016 ns`, with zero
failed, unrouted, partially routed, or overlapping nets.

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 37,534 | 70.55% | 15,666 |
| LUT as memory | 5,025 | 28.88% of LUT-RAM capacity | 12,375 |
| Slice registers | 44,655 | 41.97% | 61,745 |
| Occupied slices | 13,036 | 98.02% | 264 |
| BRAM36-equivalent tiles | 118 | 84.29% | 22 |
| DSP48E1 | 83 | 37.73% | 137 |

Relative to the AFNT checkpoint, complete copper adds 3,155 LUTs, 3,703
registers, 33.5 BRAM36-equivalent tiles, three DSPs, and 362 occupied slices.
The copper stores account for 16 RAMB36 blocks and no LUTRAM in the focused
implementation. Total LUT-memory use remains 5,025; it is dominated by small,
ported, or asynchronous memories in sprite, tile, boot-text, framebuffer, and
PS SmartConnect logic rather than the copper instruction store.

Only 22 BRAM36-equivalent tiles and 264 physical slices remain. BRAM is the
primary capacity limit for future PL features, and physical packing is also
tight. This is not a routing failure: global utilization is approximately
29.4% vertical and 34.7% horizontal, the exact design routes completely, and
the normal build flow reproduces a timing-clean bitstream. New graphics
storage must therefore justify BRAM explicitly; large memories belong in DDR
unless deterministic on-chip access is required.

### Arty tile span checkpoint

The first retained Arty RTL component is the tile span walker at source SHA-256
`2e83680312229492fd9ca9ed07353f21094c48bbf0ec787e0c4bdf9abeb97e86`.
On Beast, Vivado 2024.2 routed it out of context for `xc7z020clg400-1` against a
5.000 ns clock with 0.263 ns setup slack, 0.189 ns hold slack, and no timing
waiver. It uses 197 LUTs, 104 flip-flops, no BRAM, and no DSPs. The routed DCP
SHA-256 is
`eada9ee0f3237aeefdb98046d5dce1b03719c03e98a4677c8fd0e0dff03ddd76`.

At 200 MHz the maximum 241-span 8x8 line plan takes 1.205 microseconds before
backpressure. Directed simulation covers aligned and unaligned lines, signed
positive and negative scroll, independent wrap, 8x8 and 16x16 tiles,
backpressure, extreme signed scroll, and invalid configuration. This is valid
evidence for the span planner only. OOC ports do not have final top-level
placement, and the checkpoint says nothing yet about AXI service, line-cache
BRAM, tile palettes, composition, HDMI, or complete-system timing.

### Arty complete tile-line checkpoint

The complete tile-line builder source and routed history are identified in
`fpga/arty/graphics/TIMING_CLOSURE.md`. On Beast, Vivado 2024.2 routes the
exact `xc7z020clg400-1` component against the 5.000 ns build-clock constraint
with -0.085 ns setup slack on two endpoints and +0.027 ns hold slack. It uses:

| Resource | Used | Device percent |
|---|---:|---:|
| Slice LUTs | 1,794 | 3.37% |
| Slice registers | 1,428 | 1.34% |
| BRAM36 | 6 | 4.29% |
| DSP48 | 0 | 0.00% |

The measured 1280-pixel INDEX8 build takes 1,338 clocks at 200 MHz, leaving
3,106 clocks before the 4,444-clock 720p line deadline. This result includes
one tile layer's span planning, ordered AXI read control and response tags,
descriptor and pattern storage, pixel expansion, and four line slots. It does
not include the Zynq PS/AXI interconnect, a second layer, palettes, the final
compositor, framebuffer/sprite paths, or HDMI. The OOC design deliberately
lacks final external delays and real PS clock placement, but the measured
85 ps miss remains an explicit risk that the full integration must close.

### Arty 720p transport checkpoint

The exact Zynq PS plus fixed 1280x720p60 HDMI shell fully routes on Beast
Vivado 2024.2 with +5.393 ns setup slack, +0.160 ns hold slack, +0.538 ns
pulse-width slack, no failing endpoints, no routing errors, and no methodology
findings. It uses:

| Resource | Used | Device percent |
|---|---:|---:|
| Slice LUTs | 202 | 0.38% |
| Slice registers | 102 | 0.10% |
| BRAM36 | 0 | 0.00% |
| DSP48 | 0 | 0.00% |
| BUFG | 4 | 12.50% |
| MMCM | 1 | 25.00% |
| OSERDESE2 | 8 | n/a |

Bitstream SHA-256 is
`f8db5c827b32f202500a201e7d8ba4f01e21cdbc55259d867bbeb8c45a1e778a`.
The exact bitstream is active on Arty and physical HDMI displays the retained
full-frame test raster. These counts cover transport only, not DDR, AXI,
framebuffers, tiles, sprites, copper, blitter, geometry, or glyph hardware.

Everything below this notice is the retained ULX3S LFE5U-85F budget and route
history. It does not constrain the Arty implementation.

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

## 6C0D0CA3 K0 historical baseline

The prior committed `6C0D0CA3` 60 MHz release uses the MC68030/PMMU,
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

## 77B3CDC8 corrected K1 route

Exact corrected qualification snapshot
`77b3cdc8fddb984850073a2c2cb5998bbbe1d857` includes K-HW3, K-HW4, the K1
kernel, and the Motorola-directed processor-reset/ATC correction. NUC Yosys
maps the complete production feature set to 53,073 LUT4s, 25,532 GSR-enabled
FFs, 101 block RAMs, and 18 multipliers with zero SCCs. Exact seed-4
critical-floorplan placement finishes normally with checksum `0x7c9a8594` and
packs 66,513 TRELLIS_COMB and 25,561 TRELLIS_FF cells.

This is 471 fewer mapped LUT4s and 477 fewer packed combinational cells than
the pre-fix `66D6094F` checkpoint; block RAM and multiplier use are unchanged.
The uninterrupted exact no-waiver seed-4 router1 route finishes normally with
checksum `0x09264110` and passes every constrained clock:

| Domain | Required | Achieved |
|---|---:|---:|
| CPU | 12.500000 MHz | 14.179972 MHz |
| SDRAM | 60.002399 MHz | 61.270760 MHz |
| USB | 48.000767 MHz | 77.760498 MHz |
| pixel | 27.000029 MHz | 58.227554 MHz |
| HDMI shift | 135.025650 MHz | 294.290771 MHz |

The production `kernel_platform_v1` physical-capacity gate passes:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 66,513 (79.52%) | 17,127 |
| TRELLIS_FF | 25,561 (30.56%) | 58,079 |
| DP16KD | 101 (48.56%) | 107 |
| MULT18X18D | 18 (11.54%) | 138 |

The ECP5 LUT-permutation gate checks 13,420 cells and 17,656 routed inputs;
POR checks all 25,532 mapped FFs use GSR, and the POST font remains one DP16KD
with 11 address bits. Bitstream SHA-256 is
`56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`;
routed-JSON SHA-256 is
`a9f7c0c45ec5643d13db12bf08b03caef6434a006f02536f127d54887a4050eb`;
manifest SHA-256 is
`0593ba251da7b467e413126539d1e863ca19ef00f63843ed5f0cc6d32913b74e`.
Three independent SRAM loads of that exact bitstream pass build/ROM identity,
complete POST and 32 MiB BIST, PMMU enable, 100 Hz preemption, offender-only
fault containment, and K1 entry in 2.127-2.147 seconds. Physical HDMI displays
the exact K1 result, and the identical bitstream is now persistent; its
automatic reset-from-flash boot passes the same gate in 2.132 seconds. Physical
direct-panic HDMI/log qualification also passes with the unchanged image.
Physical supervisor-guard HDMI/log qualification passes at exact fault address
`0x02028000` as well. A physical lifecycle run passes through cycle 1,000 at
the exact 7,987-page baseline. The impractical 500,000-cycle board run was
stopped intentionally; normal ROM CRC32 `EB1B381F`, read-only AstraHost, and
the unchanged production bitstream were restored and revalidated in 2.111
seconds. Follow-on source `853ae66e300232dcbdf5f69903747faa42521114`
subsequently passes the routed five-minute candidate gate at 5,000 teardown
cycles and an independent 30-minute release gate at 29,000 cycles, both at the
exact 7,987-page baseline with coherent FPGA elapsed-time proof and an
8,809-cycle maximum masked-fault interval. Normal ROM CRC32 `BBAB0AA1`,
read-only AstraHost, and the same production bitstream are restored and
revalidated. The bounded hardware burn-in is closed. No resource, route, or
timing result changed during hardware promotion, either panic test, either
soak, or normal restoration.

## 25D9CB8E guarded-worker release

Exact source `e108a3711befa08a309f068939dff226a21c869c` retains the complete
production feature set and adds the Motorola-correct master-mode interrupt
return plus the guarded deferred kernel worker. Beast Yosys `-abc2` mapping
reports 53,079 LUT4s, 25,536 GSR-enabled FFs, 101 block RAMs, and 18
multipliers with zero SCCs. The exact strict seed-4 heap/router1 route packs:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 66,523 (79.53%) | 17,117 |
| TRELLIS_FF | 25,565 (30.57%) | 58,075 |
| DP16KD | 101 (48.56%) | 107 |
| MULT18X18D | 18 (11.54%) | 138 |

It passes every exact constraint at 15.058201 MHz CPU, 66.907532 MHz SDRAM,
79.693970 MHz USB, 53.267990 MHz pixel, and 289.771088 MHz HDMI shift. The
protected LUT-permutation gate passes 13,424 cells and 17,654 routed inputs.
Bitstream SHA-256 is
`78cd218f12feb72ccbdcb6bb141d19908c961f3438b6b559bf99b60d1c9d6940`;
routed-JSON SHA-256 is
`ef50ac0b06ea39c1ea0c09b1b7fc1d78990831557d334338bfdf33db007bee7d`.
NUC passes three independent SRAM boots, the exact five-minute/5,000-cycle
worker soak, normal-ROM restoration, a fourth SRAM boot, and automatic
reset-from-flash validation. FPGA flash now contains this exact bitstream.
Physical HDMI confirms the exact Git identity, guarded worker, PMMU,
preemption, fault containment, and K1 entry. Screenshot SHA-256 is
`e6e654d6ad0c9f5dead16f9116ab622d7a5ba731fc2fafc1ff7ba324c08128a4`.
Every hardware gate passes. This section replaces `77B3CDC8` as the routed and
persistent release baseline.

## 7DDD9C03 K9 exact route

Exact K9 source `03660014d7af6d3662504fc076700f04929117ab` was rebuilt and
routed on Beast with Yosys 0.64+159 and nextpnr-ecp5
`nextpnr-0.10-45-g98c18d7f`. It retains the complete `kernel_platform_v1`
feature set, exact 12.5 MHz CPU and 60 MHz SDRAM runtime clocks, and the policy
that physical ECP5 capacity is the only utilization limit. Yosys reports zero
SCCs, 53,079 LUT4s, 25,532 mapped FFs, 101 block RAMs, and 18 multipliers.
POR validation covers 25,536 GSR-enabled FFs. The exact strict seed-4
heap/router1 route packs:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 66,523 (79.53%) | 17,117 |
| TRELLIS_FF | 25,565 (30.57%) | 58,075 |
| DP16KD | 101 (48.56%) | 107 |
| MULT18X18D | 18 (11.54%) | 138 |

Every constrained clock passes without a waiver: 15.058201 MHz CPU,
66.907532 MHz SDRAM, 79.693970 MHz USB, 53.267990 MHz pixel, and
289.771088 MHz HDMI shift. The protected LUT-permutation gate passes 13,424
cells and 17,654 routed inputs. Exact hashes are:

| Artifact | SHA-256 |
|---|---|
| bitstream | `cf1adbe78cb9f486b3d2fbae36ada91023fda36d8cb4b0ffec7df5828e3c6bf1` |
| routed JSON | `1956c067536ce521b10cda7429ada2252af0b0e66f4d9e2debc6ac49caff9f18` |
| nextpnr report JSON | `bf160c9b72f5285c3fb6f638a4818995f41a8ca67bb839047155cf875ebb55b7` |
| FPGA configuration | `2d4683241674f13d5e0bb77769ca7fe4db8afd5f8310aeb832cc22f0fe0e95c3` |
| stage 0 | `7de247f66f2840b26692962118778cddf074f818f08dc61966a0e153439a1820` |

Two independent volatile ULX3S loads pass exact build/ROM identity, full
32 MiB POST/BIST, K9 allocator/reserve checks, K1-K8, every performance gate,
and zero overruns. Physical HDMI confirms the second run. FPGA flash remains
exact `25D9CB8E`; K9 qualification did not change the persistent image.
`7DDD9C03` is the current routed K9 release, while `25D9CB8E` remains its
persistent rollback.

### K3 software-only qualification

The 2026-07-24 K3 one-shot scheduler/deadline checkpoint based on
`8929c063cdd24c8f4f526be330549e2eb5038fc8-dirty` changes kernel, ROM,
emulator diagnostics, simulation acceptance, and hardware acceptance tooling
only. It adds no RTL and does not change the FPGA build identity. Exact
bitstream `25D9CB8E` was hash-verified, reused without repacking, and passed two
independent K3 board boots with full 32 MiB BIST and every performance gate.
Consequently the authoritative mapped, packed, free-capacity, and constrained
clock values remain exactly those in the `25D9CB8E` section above. No synthetic
resource delta is assigned to software.

### K6 software-only qualification

The hardware-qualified 2026-07-25 K6 wait-multiple checkpoint has exact source
identity
`0208cb516801fe452bf59ef053d6daa0a118ee7e-dirty-04524898314d` and changes
kernel/ROM software, emulator support, tests, and acceptance tooling only. It
adds no RTL. Exact bitstream `25D9CB8E` was hash-verified and reused without
repacking for two independent ULX3S boots. Both pass full 32 MiB BIST, PMMU
isolation, every K1-K6 marker, and all fourteen performance gates. Therefore
the mapped, packed, physical-free, bitstream-hash, and constrained-clock values
remain exactly those in the `25D9CB8E` section above. No synthetic FPGA
resource delta is assigned to K6 software.

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

## 320CAE59 routed splash checkpoint

Build `320CAE59` is the first complete hardware-rendered splash route. Beast
Yosys 0.64+159 reports zero SCCs, 53,867 LUT4s, 25,996 GSR-enabled FFs, 103
block RAMs, and 18 multipliers. The exact strict seed-4 heap/router1 route
packs:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 67,539 (80.75%) | 16,101 |
| TRELLIS_FF | 26,025 (31.12%) | 57,615 |
| DP16KD | 103 (49.52%) | 105 |
| MULT18X18D | 18 (11.54%) | 138 |

Every production constraint passes without a waiver: 14.044352 MHz CPU,
66.600067 MHz SDRAM, 79.516541 MHz USB, 55.812916 MHz pixel, and 255.623718
MHz HDMI shift. The bitstream SHA-256 is
`74d1dcaa0af7eb4fa96570a7844b64b1f29115030e6952829c50f378e364c7ce`.
This checkpoint is not a release: its first ULX3S splash presentation exposed
the Vega baseline-copy race documented in `TIMING_CLOSURE.md`, so build
`320CAE59` is rejected. These counts remain valid historical capacity evidence
for that exact source only.

## C53E68B7 routed splash repair checkpoint

Exact fixed build `C53E68B7` includes the Vega baseline-copy arbitration
repair and the complete production feature set. Beast Yosys 0.64+159 reports
zero SCCs, 53,641 LUT4s, 25,991 mapped FFs, 103 block RAMs, and 18
multipliers. The exact strict seed-4 heap/router1 route packs:

| Resource | Used | Physical free |
|---|---:|---:|
| TRELLIS_COMB | 67,295 (80.46%) | 16,345 |
| TRELLIS_FF | 26,024 (31.11%) | 57,616 |
| DP16KD | 103 (49.52%) | 105 |
| MULT18X18D | 18 (11.54%) | 138 |

Every production constraint passes without a waiver: 14.127087 MHz CPU,
67.971725 MHz SDRAM, 75.982063 MHz USB, 55.224213 MHz pixel, and 310.077515
MHz HDMI shift. The bitstream SHA-256 is
`9c6a1f575596bf612fa9649940a3c3a65758aa7e55684cebf3b42ba62c576b46`.
This is retained route and capacity evidence. It becomes a release candidate
only after repeated SRAM-only ULX3S boots and physical HDMI qualification.

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
