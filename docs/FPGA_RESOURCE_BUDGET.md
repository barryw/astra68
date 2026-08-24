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

### Arty direct-copy performance route

The retained direct RGB565 copy path changes logic only; BRAM and DSP use are
unchanged. Exact Beast Vivado 2024.2 routing at 166,666,672 Hz passes setup at
`+0.001 ns`, hold at `+0.019 ns`, and pulse width at `+0.538 ns`, with all
74,818 routable nets complete.

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs, total | 37,547 | 70.58% | 15,653 |
| Slice registers | 44,643 | 41.96% | 61,757 |
| Occupied slices | 13,035 | 98.01% | 265 |
| BRAM36-equivalent tiles | 118 | 84.29% | 22 |
| DSP48E1 | 83 | 37.73% | 137 |

This is the active Arty capacity checkpoint. Relative to the preceding copper
route it uses 13 more LUTs, 12 fewer registers, and one fewer occupied slice.
BRAM remains the primary limit.

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

### Arty HDMI audio and front-panel rejected candidate

The complete Route-13 candidate includes every qualified graphics feature,
true HDMI audio, the 512-frame asynchronous stereo FIFO, and the Arty
switch/LED front panel. The front panel reuses the shared `PNL0` implementation;
audio exposes one software-mixed sink rather than speculative per-source RTL.

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs | 39,839 | 74.89% | 13,361 |
| Slice registers | 46,308 | 43.52% | 60,092 |
| Occupied slices | 13,061 | 98.20% | 239 |
| BRAM36-equivalent tiles | 119 | 85.00% | 21 |
| DSP48E1 | 83 | 37.73% | 137 |

The design routes every net, but it is rejected at -0.960 ns setup slack
across 7,239 endpoints. Hold is +0.049 ns and pulse width is +0.538 ns. The
worst path is the existing copper-to-line-scheduler ready cone; 3.961 ns of
its 5.649 ns data delay is routing. Route 11 reached -0.011 ns with 214 free
slices and Route 12 reached -0.042 ns with 210 free slices, confirming that
the full design has exhausted reliable placement margin.

The audio endpoint itself measures 204 LUTs, 352 registers, and one RAMB36 in
the routed hierarchy. Product scope now fixes every listed feature; the earlier
per-engine hierarchy figures are diagnostic evidence, not removal candidates.

### Arty Route-14 AXI fabric reduction

Three one-to-one SmartConnect instances were generic protocol machinery, not
graphics features. Replacing HP0, HP2, and HP3 with AMD's direct AXI4-to-AXI3
protocol converter preserves every endpoint and leaves the real three-client
HP1 arbiter intact. Integrated assertions enforce the converter's 16-beat
unprotected-mode contract, and the complete graphics regression passes.

| Resource | Route 13 | Route 14 | Delta | Route-14 free |
|---|---:|---:|---:|---:|
| Slice LUTs | 39,839 | 37,317 | -2,522 | 15,883 |
| Slice registers | 46,308 | 43,829 | -2,479 | 62,571 |
| Occupied slices | 13,061 | 13,004 | -57 | 296 |
| Unique control sets | 1,236 | 1,076 | -160 | 12,224 |
| BRAM36-equivalent tiles | 119 | 119 | 0 | 21 |
| DSP48E1 | 83 | 83 | 0 | 137 |

Route 14 proves a substantial logic reduction but not adequate release margin:
physical slices remain 97.77% occupied and the exact route fails setup at
-1.064 ns. The remaining density problem is packing, especially LUTRAM and
control-set compatibility, rather than raw flip-flop capacity. The candidate
is retained as measured capacity evidence and is not deployable.

### Arty Route-15 HP1 arbitration reduction

The three scanout clients now share HP1 through a focused Astra read arbiter.
Distinct rewritten AXI IDs retain independently outstanding and interleaved
traffic; the PS boundary remains a direct protocol converter and registered
AXI3 interface. Focused arbitration tests and the complete graphics regression
pass.

| Resource | Route 14 | Route 15 | Delta | Route-15 free |
|---|---:|---:|---:|---:|
| Slice LUTs | 37,317 | 34,528 | -2,789 | 18,672 |
| Slice registers | 43,829 | 40,121 | -3,708 | 66,279 |
| Occupied slices | 13,004 | 12,571 | -433 | 729 |
| Unique control sets | 1,076 | 876 | -200 | 12,424 |
| BRAM36-equivalent tiles | 119 | 119 | 0 | 21 |
| DSP48E1 | 83 | 83 | 0 | 137 |

The exact route uses 94.52% of physical slices and connects every net, but it
is still not deployable: setup fails at -0.967 ns across 7,745 endpoints. The
worst routed path has moved to the flood renderer, so further timing work must
target that measured cone rather than AXI arbitration.

Route 16 added `extract_enable="no"` to the flood multiplier pipeline as a
measured synthesis experiment. Vivado ignored it for the inferred DSP stages:
resources and routed timing were identical to Route 15. The attribute is not
retained and creates no resource-budget change.

### Arty Route-17 registered flood arithmetic

The retained structural change drives each inferred flood DSP stage from the
preceding registered address-valid bit. It removes the prior coordinate/FSM
enable cone without adding a primitive wrapper or another arithmetic pipeline.

| Resource | Route 15 | Route 17 | Delta | Route-17 free |
|---|---:|---:|---:|---:|
| Slice LUTs | 34,528 | 34,522 | -6 | 18,678 |
| Slice registers | 40,121 | 40,078 | -43 | 66,322 |
| Occupied slices | 12,571 | 12,496 | -75 | 804 |
| Unique control sets | 876 | 891 | +15 | 12,409 |
| BRAM36-equivalent tiles | 119 | 119 | 0 | 21 |
| DSP48E1 | 83 | 83 | 0 | 137 |

Exact routing uses 93.95% of physical slices and improves WNS by 63 ps, but
still fails at `-0.904 ns`; the active limiter has moved to HP1 response decode
and tile-pattern BRAM enable. Route 17 is retained as capacity and timing-path
evidence, not as a deployable image.

### Arty Route-18 HP1 registered response boundary

HP1's PS-side R channel now uses AMD fully registered mode 1 instead of the
mode-9 SRL FIFO. AXI throughput and backpressure remain intact; the measured
SRL clock-to-Q path is removed.

| Resource | Route 17 | Route 18 | Delta | Route-18 free |
|---|---:|---:|---:|---:|
| Slice LUTs | 34,522 | 34,279 | -243 | 18,921 |
| Slice registers | 40,078 | 40,205 | +127 | 66,195 |
| Occupied slices | 12,496 | 12,584 | +88 | 716 |
| Unique control sets | 891 | 878 | -13 | 12,422 |
| BRAM36-equivalent tiles | 119 | 119 | 0 | 21 |
| DSP48E1 | 83 | 83 | 0 | 137 |

Route 18 cuts aggregate setup debt by 74.7% to `-528.551 ns` and removes the
HP1 response boundary from critical paths, but still fails WNS at `-0.859 ns`.
The LUT reduction does not translate into physical-slice savings because the
fully registered channel changes packing; retain 716 slices as the current
exact physical headroom and do not count the rejected image as deployable.

### Arty Routes 19-30 measured capacity

Routes 19-28 retain all production features while adding only measured timing
boundaries. Their exact full-route occupancy stays between 12,497 and 12,646
of 13,300 slices, with WNS improving as far as `-0.508 ns`; none is
deployable. Route 29 moves the published sprite-collision history from LUTRAM
to otherwise-free BRAM and measures:

| Resource | Route 28 | Route 29 | Delta | Route-29 free |
|---|---:|---:|---:|---:|
| Slice LUTs | 34,519 | 34,618 | +99 | 18,582 |
| Slice registers | 40,477 | 40,942 | +465 | 65,458 |
| Occupied slices | 12,547 | 12,628 | +81 | 672 |
| Unique control sets | 890 | 882 | -8 | 12,418 |
| BRAM36-equivalent tiles | 119 | 127 | +8 | 13 |
| DSP48E1 | 83 | 83 | 0 | 137 |

The BRAM conversion is functionally qualified but Route 29 fails timing at
`-0.933 ns` because the synchronous BRAM output, bank selector, and AXI read
decode shared one cycle. Route 30 restores the registered collision-read
boundary and pipelines collision address/read/commit handling. Its routed
sprite checkpoint passes 200 MHz at `+0.096 ns`; the exact integrated route
uses 34,568 LUTs, 41,556 registers, 12,531 slices, 911 control sets, 127
BRAM36-equivalent tiles, and 83 DSPs. It improves full-route WNS to `-0.438 ns`
but remains rejected and was not flashed.

Route 31's retained command-engine OOC checkpoint uses 10,650 LUTs, 13,686
registers, and 29 DSPs and routes at `-0.206 ns`. This is timing-local evidence,
not an integrated capacity update. The complete graphics regression passes;
integrated resource authority remains Route 30 until the exact Route 31 full
route completes.

The exact Route 31 full route uses 34,170 LUTs, 41,714 registers, 12,710 of
13,300 slices (95.56%), 916 control sets, 127 BRAM36 tiles, and 83 DSPs. It
connects every net but fails setup at `-0.552 ns`, so these figures replace
Route 30 as capacity evidence only; they do not describe a deployable image.

The exact Route 32 full route uses 34,199 LUTs, 41,526 registers, 12,540 of
13,300 slices (94.29%), 895 control sets, 127 BRAM36 tiles, and 83 DSPs. It
connects every net and reduces setup debt to `-0.298/-64.437 ns` WNS/TNS, but
remains rejected; these figures are capacity evidence, not a deployable image.

The exact Route 33 full route uses 34,286 LUTs, 41,674 registers, 12,781 of
13,300 slices (96.10%), 914 control sets, 127 BRAM36 tiles, and 83 DSPs. It
connects every net and reduces setup debt to `-0.271/-18.549 ns` WNS/TNS, but
remains rejected. Only 519 physical slices remain, so further timing work must
prefer removing logic depth or duplicated state over broad register insertion.

The exact Route 34 full route uses 34,390 LUTs, 41,839 registers, 12,749 of
13,300 slices (95.86%), 911 control sets, 127 BRAM36 tiles, and 83 DSPs. It
connects every net but regresses setup to `-0.364/-24.525 ns` WNS/TNS and is
rejected. The 551 free slices remain capacity evidence only; Route 35 keeps
throughput while adding only the registers required to split the measured
tile-map validation cone.

The rejected Route 35 route uses 34,433 LUTs, 41,721 registers, 12,634 of
13,300 slices (94.99%), 918 control sets, 127 BRAM36 tiles, and 83 DSPs. Despite
more nominal slice headroom, setup regresses to `-0.417/-60.621 ns`; the extra
response stage is therefore removed rather than counted as retained capacity.

The rejected Route 36 route uses 34,337 LUTs, 41,613 registers, 12,700 of
13,300 slices (95.49%), 127 BRAM36 tiles, and 83 DSPs. It connects all 69,772
routable nets, but setup regresses to `-0.480/-385.768 ns` across 3,370
endpoints. The deleted tile result state is therefore not a timing-closure
solution; these figures are capacity evidence only and do not describe a
deployable image.

The rejected Route 37 route uses 33,854 LUTs, 39,917 registers, 12,570 of
13,300 slices (94.51%), 904 control sets, 128 BRAM36-equivalent tiles, and 83
DSPs. Removing reset initialization from the pixel FIFO payload eliminates
1,696 registers and 130 occupied slices relative to Route 36, but synthesis
spends a whole RAMB36E1 on the 16x64-bit data array. The route improves to
`-0.396/-39.477 ns` across 477 endpoints but remains rejected; its figures are
capacity evidence only, not a deployable image.

The rejected Route 38 route directs that 16x64-bit array to distributed RAM.
It uses 33,914 LUTs, 40,120 registers, 12,609 of 13,300 slices (94.80%), 885
control sets, 127 BRAM36-equivalent tiles, and 83 DSPs. The exact route reaches
`-0.368/-84.862 ns` across 1,015 endpoints and remains rejected. Returning the
BRAM is retained; these figures remain capacity evidence only and do not
describe a deployable image.

The rejected Route 39 route uses 33,903 LUTs, 40,084 registers, 12,438 of
13,300 slices (93.52%), 880 control sets, 127 BRAM36-equivalent tiles, and 83
DSPs. It connects all 68,312 routable nets and reaches `-0.383/-78.332 ns`
across 722 endpoints. The removed glyph enable cone and 862 free slices are
retained capacity evidence only; the image is not deployable.

The rejected Route 40 route uses 34,044 LUTs, 40,143 registers, 12,590 of
13,300 slices (94.66%), 920 control sets, 127 BRAM36-equivalent tiles, and 83
DSPs. It connects all 68,546 routable nets and improves timing to
`-0.305/-45.555 ns`, but remains non-deployable. The classification boundary
is retained; 710 physical slices remain for measured closure work.

The rejected Route 41 no-enable mapping uses 33,999 LUTs, 40,047 registers,
12,536 of 13,300 slices (94.26%), 907 control sets, 127 BRAM36-equivalent
tiles, and 83 DSPs. Despite 764 free slices, setup regresses to
`-0.502/-16.127 ns`; the two mapping attributes are therefore removed and
these figures are capacity evidence only.

The rejected Route 42 free-running mapping uses 33,998 LUTs, 40,090 registers,
12,413 of 13,300 slices (93.33%), 895 control sets, 127 BRAM36-equivalent
tiles, and 83 DSPs. It connects all 68,367 routable nets but reaches only
`-0.395/-32.417 ns` WNS/TNS. Its 887 free slices are capacity evidence only;
the free-running glyph change is removed because Route 40 remains 90 ps better.

The rejected Route 43 flood-ready simplification uses 34,024 LUTs, 40,065
registers, 12,586 of 13,300 slices (94.63%), 896 control sets, 127
BRAM36-equivalent tiles, and 83 DSPs. It connects all 68,382 routable nets and
removes the measured HP2 response-enable cone, but remains non-deployable at
`-0.319/-41.884 ns` WNS/TNS. Its 714 free slices are capacity evidence only.

The rejected Route 44 geometry-classification pipeline uses 33,956 LUTs,
40,010 registers, 12,511 of 13,300 slices (94.07%), 886 control sets, 127
BRAM36-equivalent tiles, and 83 DSPs. It connects all 68,253 routable nets and
improves setup debt to `-0.211/-12.291 ns` across 233 endpoints. The retained
pipeline simplification returns 75 slices; the 789 free slices are capacity
evidence only until every production clock closes.

The rejected Route 45 glyph-rounding register bank uses 33,981 LUTs, 40,096
registers, 12,493 of 13,300 slices (93.93%), 889 control sets, 127
BRAM36-equivalent tiles, and 83 DSPs. Although it leaves 807 slices free and
removes the Route 44 glyph critical path, placement regresses to
`-0.550/-95.201 ns` across 1,131 endpoints. The added registers are removed;
these figures are capacity evidence only.

The rejected Route 46 shared glyph divide uses 33,969 LUTs, 40,064 registers,
12,556 of 13,300 slices (94.41%), 883 control sets, 127 BRAM36-equivalent tiles,
and 83 DSPs. It removes the rounded-divide carry chain without adding storage,
but reaches only `-0.433/-97.492 ns` across 1,076 endpoints. The change is
removed. Route 44 remains the best capacity/timing baseline, and further local
cone edits are not a credible route to production margin.

Route 44 structural analysis explains why nominal LUT/register headroom has not
translated into routable margin: 2,590 LUTRAMs, 666 SRLs, and 886 control sets
leave 2,174 register sites unused inside occupied slices while physical slice
use remains 94.07%. Sprite builder/scene storage accounts for 1,754 LUTRAMs.
The current capacity target is therefore lower packing pressure with the exact
feature set, not another isolated timing register. Candidates must demonstrate
the reduction in exact synthesis reports before entering the five-route maximum
production campaign.

The retained validator-sharing and tile-metadata checkpoints are the first
measured reductions under that policy. Exact synthesis after both changes uses
33,632 LUTs, 39,810 registers, 835 control sets, 129 BRAM36-equivalent tiles,
and 83 DSPs. Converting the phase-separated tile span/descriptor stores to four
RAMB18s removes 672 LUTs, including 576 LUTRAMs, while keeping 11 BRAM36 tiles
free. Each tile builder falls from 1,525 to 1,189 LUTs. Complete regression and
the 1,347/4,444-cycle worst-case tile deadline pass; those synthesis
checkpoints spend no production route attempt.

Structural route attempt 1 proves the area gain survives implementation:
32,874 LUTs and 39,503 registers occupy 12,401/13,300 slices (93.24%), returning
110 slices relative to Route 44. Timing fails at `-0.680/-141.162 ns` because
the new span RAMB18 output directly feeds three source-coordinate LUTs. The
route is rejected, not flashed, and consumes attempt 1/5. The next candidate
must register the BRAM read data and clear regression/synthesis gates before a
second route is considered.

The registered-prefetch candidate clears those gates. Exact synthesis uses
33,624 LUTs, 40,124 registers, 837 control sets, 129 BRAM36-equivalent tiles,
and 83 DSPs. The added 314 registers and two control sets preserve the 672-LUT
metadata reduction, and each tile builder remains at 1,185 LUTs. Synthesized
timing shows the span RAMB18 terminating directly in the prefetch FF bank with
zero LUT levels and +1.485 ns slack, so structural route attempt 2 is
authorized without changing the feature set.

Structural route attempt 2 proves the new register boundary and connects all
67,723 nets, but uses 12,432 slices (93.47%) and fails setup at
`-0.373/-35.002 ns` across 452 endpoints. The new leader is a routing-dominated
HP2-response path into the glyph fault-detail clock enable, not the removed
BRAM cone. The route is rejected and consumes attempt 2/5. No further route is
authorized until another synthesis checkpoint reduces packing or control
pressure while preserving the full feature set.

The next synthesis checkpoint clears that gate. Consolidating the sprite
admission fields into one XPM block-RAM record store yields 33,262 LUTs, 2,457
LUTRAMs, 40,106 registers, 836 control sets, 132.5 BRAM36-equivalent tiles,
and 83 DSPs. The XPM maps the exact 64x242 store to one RAMB18 plus three
RAMB36 primitives; the prior inference attempt was rejected before routing
because Vivado instead produced 81 RAM64M instances. Full regression passes,
7.5 BRAM tiles remain free, and structural route attempt 3 is authorized.

Structural route attempt 3 proves that the wide record is the wrong physical
trade: 32,661 LUTs and 40,079 registers still occupy 12,464 slices (93.71%)
with 851 control sets and 132.5 BRAM36-equivalent tiles. All nets route, but
setup regresses to `-0.390/-88.937 ns` across 779 endpoints; hold passes at
`+0.012 ns`. The route is rejected and consumes attempt 3/5. The next
candidate must remove duplicated descriptor data from this store and return
BRAM placement margin before another route is authorized.

The narrow admission-record checkpoint makes that cut. The 64-entry store now
contains only sprite index, clipped screen X, and span; preparation rereads the
authoritative descriptor. Exact synthesis uses 33,309 LUTs, 2,457 LUTRAMs,
40,072 registers, 837 control sets, 129.5 BRAM36-equivalent tiles, and 83
DSPs. The 64x28 store is exactly one RAMB18, returning three BRAM36-equivalent
tiles while preserving the LUTRAM reduction. Complete regression passes and
no production route is spent; campaign use remains 3/5.

The route-4 synthesis gate adds one shared 73-bit elastic register at the
render-command HP2 response ingress and removes reset wiring from buffered
payloads whose valid bits are clear. The full design is 33,348 LUTs, 2,457
LUTRAMs, 40,157 registers, 838 control sets, 129.5 BRAM36-equivalent tiles,
and 83 DSPs. The boundary adds only 39 LUTs, 85 registers, and one control set
over the narrow-record checkpoint while isolating all three read engines from
the routing-dominated raw HP2 metadata cone. Full regression passes; campaign
use remains 3/5 until route 4 completes.

Structural route attempt 4 uses 32,672 LUTs, including 2,347 LUTRAMs, 40,002
registers, 12,329/13,300 slices (92.70%), 875 control sets, 129.5
BRAM36-equivalent tiles, and 83 DSPs. It connects every net but fails setup at
`-0.639/-476.709 ns` across 3,108 endpoints; hold passes at `+0.012 ns`. The
new admission RAMB18 output was incorrectly allowed to drive the distributed
active-descriptor address and scene output register in one cycle. The route is
rejected, not flashed, and consumes attempt 4/5.

The retained correction adds no storage feature and reuses the existing sprite
preparation FSM to register that address before the scene read. Full regression
passes at 3,877/4,300 worst-case build cycles and 3,935 collision cycles. Route
attempt 5 is authorized by exact synthesis at 33,349 LUTs, 2,457 LUTRAMs,
40,157 registers, 838 control sets, 129.5 BRAM36-equivalent tiles, and 83 DSPs.
All six admission-RAM-to-index paths have zero LUT levels and +1.485 ns slack,
with no direct admission-RAM-to-scene-register path. Consolidating identical
clone/activation payload capture banks removes duplicated RTL state but no
net resources because Vivado already merged it. Campaign use remains 4/5 until
the fifth and final route completes.

Structural route attempt 5 uses 32,657 LUTs, including 2,348 LUTRAMs, 40,049
registers, 864 control sets, 129.5 BRAM36-equivalent tiles, and 83 DSPs. Every
net routes, but setup fails at `-0.320/-53.449 ns` across 807 endpoints; hold
passes at `+0.016 ns`. The admission-memory cone is absent. Render-command
logic owns 476 failures, led by a replicated command-type-to-writer-flush path
whose 5.031 ns delay is 82.3% routing. The image is rejected and not flashed.
Campaign use is 5/5: further routing requires a new measured structural cut,
not another seed.

The next campaign starts from a measured shared-control cut. Directly combining
the mutually exclusive render-engine writer pulses removes the Route 5 command-
type-to-flush path. Complete regression passes. Exact synthesis uses 33,391
LUTs, 40,140 registers, 838 control sets, 129.5 BRAM36-equivalent tiles, and
83 DSPs: 17 fewer registers but 42 more LUTs. The relevant netlist gate is
decisive—zero command-type-to-flush paths remain, and the new worst path into
that register has +1.572 ns slack. New campaign route use is 0/5.

Writer-control attempt 1 routes every net but fails at `-0.325/-32.435 ns`
across 409 endpoints. It uses 32,714 LUTs, 40,021 registers, 12,355 slices,
881 control sets, 129.5 BRAM36-equivalent tiles, and 83 DSPs. That is 37 more
slices than Route 5, so the candidate is removed after 1/5 rather than rerun.

Removing the five command-classifier `max_fanout=16` attributes reduced exact
synthesis to 33,289 LUTs, 40,106 registers, and 837 control sets, but exact
placement regressed to `-0.580/-299.333 ns` across 2,182 endpoints. It used
12,252 slices and 842 placed control sets; the smaller footprint did not yield
a viable timing trajectory. The attributes are restored and no route was run,
so route use remains 0/5.

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

## Compositor structural measurement (2026-08-12)

The global control-set threshold is rejected despite reducing 838 control sets
to 537: it grows synthesis by 1,106 LUTs, occupies 12,379 slices, and places at
`-0.492 ns`. A measured hierarchy cut is materially better. The seven-layer
compositor accounts for 4,217 routed LUTs while using no DSPs. Its full DSP
mapping reduces the complete design to 29,969 synthesized LUTs and 38,385
registers and to 11,293 placed slices, but does not pass its timing trajectory
gate (`-0.718 ns` placement, `-0.716 ns` after Explore physopt), so it is not
routed.

The initial reset-only measurement's 12 DSPs were stale forced attributes from
that rejected experiment; its `-0.503 ns` placement is invalid as a zero-DSP
comparison. Removing them yields 4,378 LUTs, 2,777 registers, 1,550 slices and
zero DSPs OOC. Full synthesis is 33,493 LUTs, 40,064 registers and 83 DSPs;
placement is 32,625 LUTs, 39,890 registers and 12,395 slices at
`-0.311/-203.484 ns`. That saves neither enough area nor timing margin, so the
reset rewrite is rejected before routing and only the stale-attribute cleanup
is retained.

## Sprite collision storage measurement (2026-08-13)

Eight 8x64 published collision banks consumed eight RAMB36 tiles. Mapping
only those banks to distributed RAM passes the full regression and reduces the
sprite-line OOC block-RAM total from 32.5 to 24.5 tiles. Its exact routed OOC
cost is 5,091 LUTs and 2,442 slices versus 4,741 and 2,281, with setup/hold
still passing at `+0.078/+0.028 ns`. The trade is provisional until full
placement proves that BRAM-column relief outweighs the added slice pressure;
production route use remains 0/5.

Full placement rejects the LUTRAM trade: 33,695 synthesized LUTs become 32,872
placed LUTs in 12,425 slices, with `-0.700/-394.231 ns` setup and
`-0.195/-11.535 ns` hold. The inferred storage also creates collision commit
nets with roughly 1,200--1,400 loads. The eight banks are restored to block
RAM; the measured BRAM saving does not justify the timing and routing cost.

The replacement shared only completed-frame publication: eight current-frame
banks remained parallel, while one 64x64 BRAM absorbed 64 writes during the
existing 320-cycle clear window. Complete regression passed with unchanged
collision performance. Sprite-line OOC dropped from 32.5 to 25.5 BRAM tiles,
kept LUTRAM at 931, used 4,929 LUTs and 2,363 slices, and routed at
`+0.100/+0.027 ns`.

Full placement rejects that trade too. Synthesis uses 33,509 LUTs, 40,149
registers, 852 control sets, 122.5 BRAM tiles, and 83 DSPs. Placement uses
32,577 LUTs, 39,856 registers, 12,420 slices, and 859 control sets, but fails
at `-1.285/-844.057 ns` across 3,296 setup endpoints and
`-0.237/-4.229 ns` across 97 hold endpoints. Its leading paths are
routing-dominated blitter register-to-DSP connections, not collision logic;
the seven fewer BRAM anchors destabilize DSP locality. The shared publication
is removed without a production route, leaving campaign use at 0/5.

## Blitter blend-divider measurement (2026-08-13)

The serialized source-over path had one required 8x8 product duplicated into
a three-DSP multiply/divide chain. Keeping the multiplier in one DSP and
moving the exact round-to-255 reduction to two 17-bit carry additions reduces
the routed blitter from 2,668 to 2,644 LUTs, 3,138 to 3,121 registers, and 13
to 11 DSPs. Setup improves from `+0.010` to `+0.071 ns`; the behavioral test
remains 5,822 cycles. The collision baseline is also confirmed at 32.5 BRAM
tiles after restoring its synchronous published-bank read. Integrated
placement remains the acceptance gate; production route use is 0/5.

## Production integration convergence (2026-08-13)

The current complete feature set uses 81 DSPs after the retained blitter
divider change. Production route attempt 1/5 connected every net and passed
hold, but failed setup at `-0.290 ns`; it was not flashed. Subsequent
structural work removes the measured flood-DSP clock-enable and Copper WAIT
comparison cones without removing features.

The latest exact placement is `-0.289 ns` with the next leader in framebuffer
line control. Replacing the counter's wide equality control with two registered
flags adds two flip-flops, preserves the counter, and passes framebuffer OOC
route at `+0.244 ns`. Integrated synthesis/placement remains the resource and
timing admission gate. Production route budget is 1/5.

The registered Copper WAIT candidate was functionally rejected by the
integrated raster test. Its corrected scheduler/execution split keeps all 16
Copper RAMB36s and routes Copper OOC at `+0.864 ns`. A direct comparator path
used the same resources but was rejected at `-0.726 ns` full placement and was
not routed. No feature or memory has been removed; route use remains 1/5.

The exact split placement was also rejected without routing at
`-0.460/-0.102 ns`; synthesis had rebuilt the flood bounds-to-DSP-enable cone.
An intentional operand boundary removes that cone in both OOC and full
netlists (`AREG=0`, constant `CEA2`) at 33,008 LUTs, 39,122 registers, 129.5
BRAM tiles, and 81 DSPs. Its placement is `-0.510/-0.193 ns`, led instead by a
Copper program BRAM output crossing three LUT levels into the held read-data
register. Removing only that redundant hold mux passes focused tests; Copper
OOC remains 16 RAMB36s and routes at `+0.762 ns`. It requires a fresh full
placement gate; route use remains 1/5.

The continuous Copper read does not change full resources and moves exact
placement setup from `-0.510` to `-0.299 ns`; hold is `-0.261 ns`, so it was
not routed. Its new leader was duplicated layout validation over render
command words 12--14. One seven-bit registered word-presence vector now feeds
that validation, passes the complete command regression, and removes the
targeted cone in exact OOC routing. Full placement remains the admission gate;
the feature set is unchanged and route use remains 1/5.

The shared layout facts synthesize at 33,073 LUTs, 39,127 registers, 129.5
BRAM tiles, and 81 DSPs, but exact placement is `-0.504/-0.186 ns` and is not
routed. The measured flood active-coordinate comparison is now replaced at
state decode by one registered exhaustion fact. Behavioral regressions pass
and the target path is absent from exact OOC routing. Full placement remains
the admission gate; route use remains 1/5.

The completed five-gate structural campaign retained the full feature set and
ended without another production route. Its final exact synthesis used 32,843
LUTs (61.73%), 39,124 registers (36.77%), 129.5 BRAM tiles (92.50%), and 81
DSPs (36.82%). Placement rejected at `-0.698/-0.393 ns`; 85.5% of the leading
flood-to-dispatch data delay was routing. The campaign is capped at 5/5, while
the production route budget remains 1/5.

The new structural cut removes the command processor's redundant two-entry
pixel dispatch FIFO and reuses the pixel writer's existing registered ingress,
two-entry staging, and 16-entry write FIFO. Render-command OOC now uses 10,257
LUTs, 12,014 registers, and 27 DSPs; all 45 command cases pass. Its first OOC
gate rejects at `-0.285 ns` on an unrelated pre-existing blitter address path,
so no full placement has been spent. New campaign use is 1/5 OOC and 0/5 full
placement gates.

The dispatch-removal campaign stopped at 4/5. After removing the measured
blitter address and glyph-format cones, exact full synthesis used 32,711 LUTs,
38,971 registers, 129.5 BRAM tiles, and 81 DSPs. Placement improved to
`-0.404/-0.262 ns` but exposed Copper validation's direct BRAM-output mux, so
it was not routed.

The following Copper campaign is closed at 5/5. Reusing the existing four-word
capture stage for validation costs no storage; pipelined program readback adds
66 registers and 12 LUTs to the exact full synthesis, which measured 32,697
LUTs, 38,935 registers, 129.5 BRAM tiles, and 81 DSPs. Its intermediate exact
placement was `-0.285/-0.126 ns`, but is not final-source evidence because
integration required restoring a post-capture permission state. The retained
Copper alone uses 595 LUTs, 662 registers, and 16 RAMB36s and routes OOC at
`+0.838 ns`. No route or flash was produced; the next campaign starts at 0/5.

The closed glyph/ready/blitter campaign's exact full synthesis uses 32,056
LUTs (60.26%), 38,701 registers (36.37%), 129.5 BRAM tiles (92.50%), and 81
DSPs (36.82%). Its retained blitter change adds no arithmetic block: both
blend phases share the existing multiplier and registered `/255` result.
Placement rejected at `-0.605/-0.184 ns`; the corrected render-command OOC
block rejects at `-0.227 ns` on completion control. No fifth full gate,
production route, bitstream, or flash was spent. Production route use remains
1/5.

The following completion-boundary campaign stopped at 4/5 without a new full
resource checkpoint. Its retained changes are register/control boundaries,
not feature cuts. Exact render-command OOC reached `+0.006 ns`; an attempted
extra DSP operand stage was rejected and reverted when Vivado retained
`AREG=0`. No capacity, route, bitstream, or hardware claim follows from that
isolated result.

Capacity work now uses physically constrained reusable checkpoints plus
incremental full integration. The complete design remains the authority for
the 129.5-BRAM and 81-DSP placement problem; unconstrained leaf OOC utilization
and timing are diagnostic only. No feature or reserved resource is removed,
and production route use remains 1/5.

The closed incremental pilot's final exact route uses 32,115 LUTs (60.37%),
38,914 registers (36.57%), 129.5 BRAM tiles (92.50%), and 81 DSPs (36.82%).
All 66,216 routable nets complete with zero errors. The design is therefore
not rejected for LUT, register, or DSP capacity; BRAM remains the tight static
resource and local routing remains the timing constraint. Exact timing is
`-0.283/+0.010 ns`, so the fail-closed build intentionally produced no
bitstream. No feature or reserved resource was removed.

Zero-slack convergence run 1 retains every feature and measures 32,330 LUTs
(60.77%), 38,942 registers (36.60%), 129.5 BRAM tiles (92.50%), and 81 DSPs
(36.82%). All 66,540 routable nets complete, but setup rejects at `-0.582 ns`.
This is a strategy-control checkpoint, not a capacity failure: plain
`Performance_Explore` omitted the required post-route physical-optimization
stage. Campaign use is 1/5 and no bitstream was written.

Zero-slack convergence run 2 retains the full feature set and fully routes
66,517 nets. Post-route timing is `-0.607/+0.010 ns` with 1,114 failing setup
endpoints, so the fail-closed gate writes no bitstream. The failure is an HP2
response-control boundary, not a resource-capacity result. Campaign use is
2/5.

The run-3 candidate changes only response/control boundaries and adds one
registered 64-bit spill beat; it removes no feature or reserved resource. Its
exact OOC diagnostic improves from `-0.303 ns` to `-0.080 ns`, but OOC use is
not a capacity claim. Full integration campaign use remains 2/5 until run 3
produces the next exact utilization checkpoint.

Zero-slack convergence run 3 uses 32,325 LUTs (60.76%), 39,001 registers
(36.66%), 129.5 BRAM tiles (92.50%), and 81 DSPs (36.82%). All 66,760 nets
route, so capacity is not the rejection cause; setup is `-0.500 ns` while
hold is `+0.010 ns`. Campaign use is 3/5 and no bitstream was written. The
run-4 boundary corrections remove no feature or reserved resource.

Zero-slack convergence run 4 retains all features and routes all 66,804 nets
with zero errors. Setup rejects at `-0.347 ns`; hold passes at `+0.010 ns`.
BRAM remains 129.5 tiles (92.50%), and Vivado's QoR analysis explicitly marks
that density as a timing-closure risk while finding no level-5 congestion
window. Campaign use is 4/5 and no bitstream was written.

Zero-slack convergence run 5 retains every feature while applying Vivado's
measured QoR resource mapping. BRAM falls to 105 tiles (75.00%); the exact
synthesis uses 37,471 LUTs (70.43%), 40,547 registers (38.10%), and 81 DSPs
(36.82%). All 69,178 routable nets complete with zero errors and hold passes at
`+0.017 ns`, proving that capacity and routing completeness are no longer the
release blockers. Setup remains `-0.277 ns` across 153 endpoints, dominated by
sprite clear/copy and working-line memory control, so the fail-closed gate
writes no bitstream. Campaign use is 5/5. The next campaign begins at 0/5 with
the full feature and 105-BRAM mapping retained.

Sprite structural gate 1/5 is resource-diagnostic only: the isolated builder
retains all 37 of its block-RAM primitives and removes no sprite capability.
It routes completely at `-0.004/+0.028 ns`; its missing top-level clock root
and the full design's QoR-distributed memory mapping prevent treating that OOC
count as a new integrated capacity result. No full placement has been spent in
the new campaign.

Gate 2/5 keeps the same 37 sprite BRAM primitives and passes exact OOC setup at
`+0.147 ns`. The retained preload change adds no state, storage, or latency;
the rejected one-cycle load experiment is not in the source. Full regression
passes, so the next exact full implementation will determine the integrated
105-BRAM resource checkpoint. Campaign use is 2/5 and no full implementation
has yet been spent.

Gate 3/5 stopped before synthesis on an incomplete Beast source mirror and
therefore supplies no resource checkpoint. No feature was removed and no
bitstream was written. Campaign use is 3/5.

Gate 4/5 retains the complete production feature set and uses 36,664 LUTs
(68.92%), 40,375 registers (37.95%), 105 BRAM tiles (75.00%), and 81 DSPs
(36.82%). All 69,166 routable nets complete with zero errors, so capacity and
route completeness pass; setup alone rejects at `-0.208 ns` while hold passes
at `+0.010 ns`. The leading measured cost is a long protected blitter register
net, not feature capacity. Campaign use is 4/5 and no bitstream was written.

Gate 5/5 retains every feature and uses 36,714 LUTs (69.01%), 40,412 registers
(37.98%), 105 BRAM tiles (75.00%), and 81 DSPs (36.82%). All 69,287 routable
nets complete with zero errors and hold passes at `+0.013 ns`; setup rejects at
`-0.191 ns`. The campaign is closed with no bitstream. The next measured
capacity experiment restores only the eight 512x32 sprite working memories to
their requested block-RAM mapping. Their expected eight-tile cost leaves 27
BRAM tiles free; exact synthesis, not that estimate, is the gate.

Working-memory gate 1/5 supplies that exact synthesis result: each of the eight
320x32 memories uses one RAMB18E1, for four additional BRAM36-equivalent tiles.
The full design therefore uses 109/140 tiles (77.86%), leaving 31 tiles free.
The complete feature set and graphics regression remain intact; exact route is
the next timing and release gate.

Working-memory gate 2/5 confirms the same full routed capacity: 34,303 LUTs
(64.48%), 40,065 registers (37.66%), 109 BRAM tiles (77.86%), and 81 DSPs
(36.82%). All 67,466 routable nets complete. Capacity is not the rejection;
setup is `-0.171 ns` while hold passes at `+0.050 ns`, and no bitstream is
written.

Working-memory gate 3/5 is diagnostic and changes no reserved memory or feature
budget. Exact render-command OOC uses 10,461 LUTs, 12,147 registers, and 27
DSPs; those isolated counts are not a replacement for the gate-2 full-design
capacity checkpoint.

Working-memory gate 4/5 retains the same 109 BRAM tiles and 81 DSPs, routes all
67,525 nets, and rejects only setup at `-0.150 ns`; hold is `+0.033 ns`.
Capacity and route completeness remain passing release gates.

Working-memory gate 5/5 uses 34,390 LUTs (64.64%), 40,108 registers (37.70%),
109 BRAM tiles (77.86%), and 81 DSPs (36.82%). It routes all 67,539 nets but
rejects setup at `-0.245 ns`; hold is `+0.005 ns`. The next one-hot glyph-FSM
experiment changes control encoding, not any reserved feature or memory budget;
OOC resource deltas are diagnostic until exact full integration.

Glyph-FSM gate 1/5 is OOC-only and supplies no new full capacity claim. Its
10,306 LUTs, 12,103 registers, and 27 DSPs also prove the one-hot hint had no
enforceable FSM effect; explicit encoding is the next diagnostic gate.

Glyph-FSM gate 2/5 is also OOC-only and supplies no new full capacity claim.
Explicit 55-bit one-hot state uses 10,912 LUTs, 12,193 registers, and 27 DSPs,
but is rejected on timing at `-1.841 ns`. Gate 3 returns to compact RTL and
tests tool-managed recoding; the last exact full capacity checkpoint remains
34,390 LUTs, 40,108 registers, 109 BRAM tiles, and 81 DSPs.

Engine-response gate 1/5 is OOC-only and supplies no new full capacity claim.
It uses 10,311 LUTs, 12,125 registers, and 27 DSPs while proving bounded local
replication of one response-valid bit. The complete graphics regression passes;
the last exact full capacity checkpoint remains 34,390 LUTs, 40,108 registers,
109 BRAM tiles, and 81 DSPs until gate 2 full integration.

Engine-response gate 2/5 supplies the new exact full capacity checkpoint:
34,294 LUTs (64.46%), 40,168 registers (37.75%), 109 BRAM tiles (77.86%), and
81 DSPs (36.82%). All 67,615 nets route; capacity is not the rejection cause.
Setup is `-0.185 ns` while hold passes at `+0.041 ns`. Gate 3 changes enable
mapping on one existing 32-bit control register and reserves no new feature or
memory capacity.

Engine-response gate 3/5 is OOC-diagnostic only: 1,544 LUTs, 3,418 registers,
zero BRAM tiles, and three DSPs. It changes enable mapping on existing control
registers and supplies no new full capacity claim; gate 2 remains authoritative
until gate 4 exact integration.

Engine-response gate 4/5 supplies the new exact full capacity checkpoint:
34,378 LUTs (64.62%), 40,172 registers (37.76%), 109 BRAM tiles (77.86%), and
81 DSPs (36.82%). All 67,641 nets route and hold passes at `+0.050 ns`; capacity
is not the rejection cause. Setup regresses to `-0.265 ns`, so the broad
32-bit enable-mapping experiment is rejected. Gate 5 changes storage mapping
only for the existing eight-bit tile transparent index and reserves no new
feature or memory capacity.

Engine-response gate 5/5 OOC uses 1,573 LUTs, 3,418 registers, zero BRAM tiles,
and three DSPs. This is diagnostic, not a new full capacity claim. It proves
the eight-bit targeted mapping without reserving memory or removing features;
gate 4 remains the exact full capacity checkpoint until the authorized final
production route completes.

Glyph-FSM gate 4/5 was also stopped after synthesis failed its recognition
gate. It supplies no routed OOC or full-design capacity claim and leaves the
last exact full capacity checkpoint unchanged.

Glyph-FSM gate 5/5 is stopped after the same synthesis-recognition failure and
supplies no routed capacity claim. The campaign closes with no bitstream and no
change to the last exact full checkpoint. The next response-valid replication
experiment adds no feature or reserved memory and remains OOC-diagnostic until
full integration.

Glyph-FSM gate 3/5 was stopped after synthesis failed its FSM-recognition gate.
It supplies no routed OOC or full-design capacity claim and changes no reserved
feature or memory budget. The last exact full capacity checkpoint remains
34,390 LUTs, 40,108 registers, 109 BRAM tiles, and 81 DSPs.

Engine-response gate 5/5 exact full use is 34,204 LUTs (64.29%), 40,148
registers (37.73%), 109 BRAM tiles (77.86%), and 81 DSPs (36.82%). All 67,228
nets route and hold passes at `+0.012 ns`; capacity is not the rejection cause.
Setup is `-0.294 ns`, so the split-register experiment is removed and reserves
no capacity. The retained capacity authority returns to gate 2: 34,294 LUTs,
40,168 registers, 109 BRAM tiles, and 81 DSPs. The next registered one-bit
write-select experiment adds no memory or feature reservation and remains
OOC-diagnostic until exact full integration.

MMIO-predecode gate 1/5 is OOC-diagnostic: 1,517 LUTs, 3,419 registers, zero
BRAM tiles, and three DSPs. It adds one register while reducing the isolated
control block by 27 LUTs versus engine-response gate 3 OOC. It reserves no
memory or feature capacity; engine-response gate 2 remains the authoritative
full capacity checkpoint until gate 2 exact integration.

MMIO-predecode gate 2/5 is the new exact full capacity checkpoint: 34,141 LUTs
(64.17%), 40,073 registers (37.66%), 109 BRAM tiles (77.86%), and 81 DSPs
(36.82%). All 67,080 nets route and hold passes at `+0.017 ns`; capacity and
route completeness pass. Setup alone rejects at `-0.111 ns`, so no bitstream
is written. Gate 3 changes only equivalent geometry-opcode decode and reserves
no feature or memory capacity.

MMIO-predecode gate 3/5 is OOC-diagnostic: 10,374 LUTs, 12,117 registers, zero
BRAM tiles, and 27 DSPs. It changes no storage or feature budget and proves the
target geometry path at `+1.821 ns`; gate 2 remains the exact full capacity
checkpoint until gate 4 integration.

MMIO-predecode gate 4/5 is the new exact full capacity checkpoint: 34,179 LUTs
(64.25%), 40,159 registers (37.74%), 109 BRAM tiles (77.86%), and 81 DSPs
(36.82%). All 67,162 nets route and hold passes at `+0.013 ns`; setup alone
rejects at `-0.101 ns`. Gate 5 reuses the existing Copper validation state and
adds no storage, feature, or reserved-capacity requirement.

MMIO-predecode gate 5/5 OOC uses 590 LUTs, 662 registers, 16 BRAM tiles, and no
DSPs. It preserves the established Copper memory allocation and adds no state
or reserved capacity. Gate 4 remains the exact full capacity checkpoint until
the final production integration completes.

MMIO-predecode gate 5/5 exact full use is 34,152 LUTs (64.20%), 40,177
registers (37.76%), 109 BRAM tiles (77.86%), and 81 DSPs (36.82%). All 67,130
nets route and hold passes at `+0.007 ns`; capacity and route completeness pass.
Setup rejects at `-0.230 ns`, so no bitstream exists and the campaign closes.
This becomes the latest exact capacity measurement; gate 4 remains the best
exact timing checkpoint. The next response-error/glyph-enable experiment adds
no feature or reserved-memory capacity until exact OOC measurement proves it.

The full-feature 166,666,672 Hz hardware baseline uses 33,409 LUTs (62.80%),
40,069 registers (37.66%), 109 BRAM tiles (77.86%), and 81 DSPs (36.82%). All
66,837 nets route with zero errors and both setup (`+0.144 ns`) and hold
(`+0.019 ns`) pass. No feature or memory reservation changed; the lower LUT
count is synthesis/placement mapping at the qualified clock point. A valid
bitstream and XSA exist and are hardware-qualified as build `0x18EBE2E1`.

The retained 16-sprites-per-scanline design keeps all 64 descriptors and caps
each line at 16 admitted spans plus 2,048 destination pixels. Its final exact
200 MHz route uses 32,289 LUTs (60.69%), 38,942 registers (36.60%), 129.5 BRAM
tiles (92.50%), and 81 DSPs (36.82%). All 66,393 nets route with zero errors
and hold passes at `+0.050 ns`; setup rejects at `-0.152 ns`, so this is a
capacity measurement only and no bitstream exists. The five-attempt campaign
is closed. The qualified 166,666,672 Hz checkpoint remains release authority.

## Full-feature 187.5 MHz release candidate (2026-08-14)

The exact 187,500,000 Hz candidate retains the complete production feature
set and uses the following routed resources:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs | 31,957 | 60.07% | 21,243 |
| Slice registers | 39,070 | 36.72% | 67,330 |
| Occupied slices | 12,263 | 92.20% | 1,037 |
| BRAM36-equivalent tiles | 129.5 | 92.50% | 10.5 |
| DSP48E1 | 81 | 36.82% | 139 |

All 66,520 routable nets connect with zero errors; exact setup/hold slack is
`+0.173/+0.009 ns` and pulse-width slack is `+0.538 ns`. The implementation
used a 200 MHz margin target but the shipped PS7 FCLK1, FSBL, device tree, and
release timing authority are exactly 187.5 MHz. Capacity is therefore closed
for this candidate. The exact 187.5 MHz image is now the active hardware
authority after three successful boots and repeated renderer, Copper, sprite,
and HDMI-audio qualification. No resource figure changed during qualification;
the two retained fixes affect only ARM-side certification software.

## Active front-panel/reset 187.5 MHz release (2026-08-15)

The active exact production route adds the complete Arty front panel without
removing any graphics or HDMI-audio feature:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs | 31,904 | 59.97% | 21,296 |
| Slice registers | 39,006 | 36.66% | 67,394 |
| Occupied slices | 12,203 | 91.75% | 1,097 |
| BRAM36-equivalent tiles | 129.5 | 92.50% | 10.5 |
| DSP48E1 | 81 | 36.82% | 139 |

All 66,515 nets route with zero errors; setup, hold, and pulse-width slack are
`+0.250/+0.010/+0.538 ns`. This supersedes the prior 187.5 MHz candidate as
the current capacity authority. The front panel changes no BRAM or DSP budget;
its seven output channels, six inputs, ownership/atomic control, and activity
timer fit within the slightly lower routed LUT, register, and slice totals.

## Rejected cached-RGB565 copy stream (2026-08-20)

The exact experimental full route used 32,123 LUTs, 39,069 registers, 12,346
slices, 129.5 BRAM tiles, and 81 DSPs. It connected all 66,652 nets and passed
setup/hold/pulse width at `+0.122/+0.048/+0.538 ns` after post-route physical
optimization. The isolated blitter used 2,906 LUTs, 3,215 registers, zero BRAM
tiles, and 11 DSPs at `+0.090/+0.110 ns`.

These figures are rejection evidence, not reserved capacity. The physical
desktop exhibited a visible mid-screen wrap on its full-width compositor BLIT,
so the stream was removed and no candidate resource is retained. The active
capacity authority remains the front-panel/reset release above.

## Rejected final-pixel signature (2026-08-20)

Six bounded final-pixel signature forms fully routed but failed setup timing.
The final CRC16 form used 32,287 LUTs, 39,070 registers, 12,423
slices, 129.5 BRAM tiles, and 81 DSPs, with `-0.229/+0.021/+0.538 ns`
setup/hold/pulse-width slack. It was not converted to a bitstream. All signature
RTL and MMIO state were removed, so the active resource budget remains the
qualified front-panel/reset release above.

## HDMI startup 200 MHz over-target checkpoint (2026-08-21)

The full standards-based HDMI startup candidate at an actual 200 MHz FCLK1
uses 32,245 LUTs (60.61%), 39,004 registers (36.66%), 12,299 slices (92.47%),
129.5 BRAM36-equivalent tiles (92.50%), and 81 DSPs. All 66,445 nets route with
zero errors, hold passes at `+0.033 ns`, and setup rejects at `-0.185 ns`.
No bitstream exists. This is the latest exact capacity measurement; the active
capacity/release authority remains the timing-clean 187.5 MHz front-panel
release until the exact HDMI-startup production configuration closes and is
qualified on hardware.

## HDMI startup production-clock candidate (2026-08-21)

The exact 187.5 MHz production candidate uses 31,951 LUTs (60.06%), 39,064
registers (36.71%), 12,249 slices (92.10%), 129.5 BRAM36-equivalent tiles
(92.50%), and 81 DSPs (36.82%). All 66,591 routable nets connect with zero
errors; release-clock setup/hold/pulse-width slack is
`+0.055/+0.034/+0.538 ns`. It retains the complete graphics, front-panel, and
HDMI-audio feature set and adds the native PS7 I2C0/GPIO HDMI startup path.

The optional 200 MHz implementation margin misses setup by `0.278 ns`; the
actual generated and shipped FCLK1 remains exactly 187.5 MHz. This candidate
supersedes the rejected 200 MHz capacity checkpoint, but it does not become
active release authority until physical board qualification passes.

## Generic framebuffer-copy candidate (2026-08-21)

The exact full-feature route with the generic 64-bit AXI block-copy engine uses:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs | 32,548 | 61.18% | 20,652 |
| Slice registers | 39,402 | 37.03% | 66,998 |
| Occupied slices | 12,253 | 92.13% | 1,047 |
| BRAM36-equivalent tiles | 129.5 | 92.50% | 10.5 |
| DSP48E1 | 81 | 36.82% | 139 |

All 67,294 routable nets connect with zero errors. Actual 187.5 MHz
setup/hold slack passes at `+0.070/+0.011 ns`; the 200 MHz implementation
target is `-0.263 ns` and remains margin evidence, not the shipped clock.
Compared with the qualified HDMI-startup route, the candidate adds 597 LUTs,
338 registers, and four occupied slices while changing no BRAM or DSP
allocation. Physical board qualification now passes the exact overlap copy,
production-width offset check, cold boot, HDMI hot-plug, and 48 kHz stereo
audio gates, so this is the active capacity authority.

## Qualified AXI lane-realignment candidate (2026-08-23)

The exact full-feature, non-incremental route with byte-lane realignment and
the local sprite-start pipeline uses:

| Resource | Used | Device percent | Physical free |
|---|---:|---:|---:|
| Slice LUTs | 33,176 | 62.36% | 20,024 |
| Slice registers | 39,686 | 37.30% | 66,714 |
| Occupied slices | 12,296 | 92.45% | 1,004 |
| BRAM36-equivalent tiles | 129.5 | 92.50% | 10.5 |
| DSP48E1 | 81 | 36.82% | 139 |

All 68,015 routable nets connect with zero errors. The actual 187.5 MHz
setup/hold/pulse-width result is `+0.022/+0.018/+1.416 ns`; the 200 MHz stress
setup result is `-0.311 ns`. This is the latest exact capacity authority. The
combined STOR v9 and output-packing image closes the former application blocker
at a 779.731 ms median and exactly three presentation batches across 20/20
physical runs. Renderer, all 64 lanes, exact compositor, screen offset, sprite,
Copper, POST/SDRAM, stage 8, event-snapshot recovery, and 48 kHz audio pass on
the Arty attached to Beast. The route remains a volatile FPGA-manager load;
the generic framebuffer-copy route remains the persistent rollback authority
until HDMI hot-plug and visual inspection can be repeated with a sink attached
to Beast.
