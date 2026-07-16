# Astra 68 current engineering state

This is the short continuation map for the active machine. It records decisions
and validated boundaries that are easy to lose across long sessions. Detailed
contracts remain authoritative in the linked documents; historical handovers
and old resource tables are not current status.

## Locked architecture

- The sole RTL CPU is the repaired TG68K.C MC68030 integer core with integrated
  PMMU in `fpga/cpu/tg68k_c_030_mmu2`. There is no selectable fallback core,
  FPU, or cycle-accuracy requirement.
- Motorola MC68030 documentation is the architectural authority. Emulator,
  Amiga, MiSTer, upstream, and test behavior may expose a bug but never justify
  an Astra-specific CPU semantic. See [MC68030_COMPLIANCE.md](MC68030_COMPLIANCE.md).
- The core is a strong development baseline, not an unconditional production
  claim. SCC elimination, shared conformance, boot, SDRAM, and retained hardware
  tests pass substantial coverage. Remaining valid exception/restart and
  fault-on-stacking cases stay fail-closed until resolved.
- Portable CPU/PMMU expectations live in `conformance/` and run through the
  canonical Musashi and RTL adapters. Do not create separate expected results
  in an adapter. White-box RTL tests remain complementary for implementation
  invariants that Musashi cannot observe.

## Locked platform boundaries

- The production clocks are 12.5 MHz CPU and 75 MHz SDRAM. Correctness,
  architecture, and the requested graphics feature set are not traded for a
  lower clock or an unconstrained bitstream.
- The machine has 32 MiB of SDRAM at `0x02000000..0x03ffffff`. The 68030 logical
  address space remains 32-bit; the PMMU provides process isolation and virtual
  memory. The 32 MiB value is physical RAM, not a 24-bit CPU address limit.
- The production ROM image is `/ASTRA68.ROM` on the existing FAT/exFAT volume.
  AstraHost reads only that file and preserves unrelated card data. Stage 0 is
  the small immutable FPGA loader; the ROM executes from its SDRAM-backed
  overlay. Astra's native filesystem will use a separate raw partition through
  a versioned block service.
- Every ESP32-to-FPGA application, boot, storage, network, input, and control
  transaction uses AstraHost SPI. UART is not a fallback transport. The FPGA
  FTDI diagnostic UART and ESP32's own logging console are independent and may
  be used for POST and bring-up.
- CPU-visible MMIO assignments are centralized in
  [MEMORY_MAP.md](MEMORY_MAP.md). Software uses NDK interfaces and resource
  ownership rather than baking register addresses into applications.

## Hardware and build topology

- The ULX3S is physically attached to `nuc` at `barry@192.168.1.2`. Flashing,
  FTDI access, HDMI capture, SD maintenance, and hardware acceptance happen
  there. Do not probe Beast or the Mac for the board.
- Beast is the primary high-throughput synthesis/simulation host. The Mac and
  NUC are useful for independent placement/router coverage. Transfer immutable
  artifacts with `rsync`; access remotes through `ssh`.
- Do not treat `/home/barry/astra68` on either remote as a clean canonical
  checkout. On 2026-07-15 NUC's copy was a dirty historical `harte-harness`
  branch, and Beast's path was not a Git worktree. Do not pull, reset, clean, or
  release from either path. Build and test pushed `main` from a fresh immutable
  `/tmp/astra68-<checkpoint>` bundle, and keep the older remote state intact.
- Beast has the intended GCC, m68k cross compiler, GHDL/OSS CAD, and static
  analyzer, but its system Python lacks `pytest` and it has no Docker. Run
  shared architecture and Harte targets directly there when appropriate. The
  Mac's pinned Docker/OrbStack path is the verified NDK documentation builder;
  do not weaken the GCC analyzer or docs gate to fit the wrong host.
- OSS CAD revisions differ between hosts. A route made with another nextpnr or
  Yosys revision is useful diversity, not a controlled same-seed comparison.
  Record the exact host and tool identities with every retained artifact.
- Never clone a mutable build directory with hard links or `rsync --link-dest`.
  Yosys and ROM staging overwrite generated files in place and can silently
  mutate an earlier checkpoint. Snapshot sources independently and keep routed
  JSON artifacts immutable.

## Current integration state

- POST, front-panel MMIO, cache coherence, byte/address lanes, 32 MiB SDRAM
  BIST, Astraea DMA, SDRAM-backed ROM handoff, and kernel entry have exact
  simulation and retained hardware baselines.
- Astraea and Vega implement the requested framebuffer, tile, sprite, copper,
  blitter, primitive drawing, and scanout paths. Directed tests and integrated
  normal/INDEX8/RGB565 workloads are exact. Lyra and remaining Vesta services
  still need fabric budget.
- The enforced `core_graphics` packing ceiling is 65% of the ECP5-85K, not the
  physical device limit. The `complete_chipset` target is 75%, with an absolute
  80% stop. See [FPGA_RESOURCE_BUDGET.md](FPGA_RESOURCE_BUDGET.md); historical
  audit tables are not current capacity numbers.
- The active blocker is promotion of the first timing-clean diagnostic route
  into a reproducible nonzero-build-ID bitstream and board acceptance, not a
  known functional failure. Earlier P39 packed under the 65% profile at 54,003
  cells, but its best SDRAM route was only 68.65 MHz. P41
  removed the live blitter-state cone but is 73 cells over profile and its
  first route exposed a worse registered Vega-lock path. P44's 11 overlapping
  registered interface facts pass every exact functional reference and reduce
  packing to 54,399, still 33 cells over profile. Its first complete route
  passes the CPU at 14.23 MHz and reaches 70.90 MHz SDRAM; the new failed cone
  is the blitter's `rows - 1` carry chain feeding its validation multiplier.
  Mac and NUC routes reach 70.02 and 68.98 MHz on the recurring Vega-lock and
  sprite-qualification boundaries. P44 is fully measured and rejected. P45
  registers the measured row-count multiplier operand using the existing row
  counter; all exact functional gates pass, mapping drops by 160 LUT4s, and
  packing reaches 54,345, only 21 cells inside the profile. The first P45
  placement matrix was stopped after
  the floorplan report proved that its validation multiplier had been
  constrained into a region with zero DSP sites. The control region now
  includes the adjacent measured DSP row, and enforced region capacity is a
  fail-fast invariant rather than a warning. The corrected Beast seed-23
  router1 route passes CPU at 13.8165 MHz but reaches only 71.7360 MHz SDRAM.
  Timing-driven ripup improves that placement only to 72.0098 MHz, while
  independent Beast seed 4 repeats the same cone at 65.6125 MHz. The P44
  subtract-to-multiplier path is gone; the replacement path begins at
  `chunk_count_mem[4]`, crosses the blitter's
  `issue_count_mem < chunk_count_mem` request-valid comparison, and ends in
  shared SDRAM arbitration. NUC seed 57 reaches 66.0284 MHz on an independent
  Draw shared-ellipse-ALU path. P45 is fully measured and rejected.
  P46 removes the redundant live issue-count comparison because the registered
  issue-state fact retires on the same edge as the final accepted request; a
  simulation assertion preserves that invariant. Directed graphics,
  integrated normal/INDEX8/RGB565, boot, DMA, POST, and kernel-entry results
  remain cycle exact. The frozen Beast synthesis maps 43,435 LUT4s and 18,253
  FFs with zero SCCs, then packs to 54,191 `TRELLIS_COMB`: 154 fewer than P45
  and 175 cells inside the profile. The P45 comparator cone is absent from the
  completed P46 routes, so that correction worked. Beast seed 23 reaches
  14.0087/70.2001 MHz on Draw's full-byte glyph-opcode decode; Mac seed 33
  reaches 13.2642/69.1419 MHz on the SDRAM core's dynamic open-row-hit read
  path; NUC seed 57 reaches 13.6567/68.8705 MHz on tile-builder state crossing
  Vega owner selection and request-queue update. P46 is fully measured and
  rejected.
  P47 exploits the contiguous glyph opcodes 8..11 inside glyph-only states and
  decodes their two low mode bits rather than rebuilding an eight-bit opcode
  comparison. All exact P46 functional and cycle references still pass. Beast
  Yosys `0.64+159` maps 43,290 LUT4s and 18,253 FFs with zero SCCs, then packs
  to 53,966 `TRELLIS_COMB`: 225 fewer than P46 and 400 cells inside the active
  profile. Beast seed 23 routes at 13.893520 MHz CPU and 66.684448 MHz SDRAM.
  The intended glyph-decode path is gone; the replacement path is internal to
  the SDRAM core, where ACTIVATE/PRECHARGE rebuild the already captured target
  through combinational `target_state_r` before updating row-open state. P47 is
  rejected by that route. Mac seed 33 also fails at 14.088277/69.008347 MHz on
  tile-builder state crossing Vega selected-owner arbitration into sprite
  response state. NUC seed 57 also fails at 13.381866/67.042099 MHz; its path
  runs from Astraea's registered `mem_owner[0]` through shared client
  ready/selection logic into blitter `issue_dst_ptr_mem[1]`. P48 consumes
  registered `target_state_q` in
  those exact decisions without adding a state or changing transaction timing.
  Directed graphics, integrated normal/INDEX8/RGB565, SDRAM, boot, DMA, POST,
  and kernel-entry results remain exact. Its frozen Beast synthesis maps 43,365
  LUT4s, 18,252 FFs, 3,826 carry cells, 80 BRAMs, and 17 multipliers with zero
  final SCCs. Beast seed-23 placement packs 53,957 `TRELLIS_COMB`, nine fewer
  than P47 and 409 inside the profile, with every enforced region physically
  legal. Its first full route passes CPU at 13.206027 MHz and reaches 74.889542
  MHz SDRAM against 75.007500 MHz, a real 0.021 ns miss. The targeted P47 SDRAM
  path is gone; the remaining path runs from Draw `state[5]` through operand
  selection and the shared 48-bit ellipse add/sub chain into `ellipse_dx[47]`.
  Same-placement timing ripup then completes normally and passes every clock:
  13.219120 MHz CPU and 75.930145 MHz SDRAM. The new 13.17 ns SDRAM path runs
  from Draw `pixel_result_mem[10]` through pixel-value and state/writeback
  selection, with 10.27 ns routing and 2.90 ns logic. This is the first
  complete-graphics diagnostic timing pass, but its zero build ID, exploratory
  command line, and roughly 0.16 ns SDRAM slack are not release evidence.
  A cycle-exact attempt to replace the ellipse ALU's six-bit state decode with
  its two-bit phase plus one registered X/Y selector passes every functional
  gate but maps 363 more LUT4s and 57 more carry cells than P48. It is rejected
  before placement; P48 remains the release candidate. The independent Mac
  seed-33 P48 route is also complete and rejected at 13.5285 MHz CPU and
  65.3723 MHz SDRAM. Its 15.30 ns Draw path starts at `state[4]`, crosses the
  deep next-state mux, and spends 11.212 ns in routing plus 4.085 ns in logic.
  All P47/P48 diversity jobs are now complete.
  Placement estimates remain diagnostic only.
  Continue from [TIMING_CLOSURE.md](../fpga/soc/oss_flow/TIMING_CLOSURE.md)
  instead of repeating old seeds or speculative floorplans.
- P48 now has one complete-graphics diagnostic route that passes every
  production clock. No release-identical P48 bitstream has been packaged or
  accepted on hardware yet. A board that currently reaches POST is evidence for
  the retained earlier hardware baseline, not evidence for this active netlist.
- The canonical entry points now agree on divider 0, 12.5 MHz CPU, seed 23,
  heap timing weight 20, router1 timing ripup, and the measured critical
  floorplan. The build ID covers all supported synthesis, placement,
  floorplan, router, and resource-profile controls plus stage-0 sources. The
  split flow packages only a timing-clean final route, and its manifest binds
  both stage 0 and `/ASTRA68.ROM`. This flow still needs to be committed and
  exercised by the nonzero-ID release rerun before it counts as evidence.

## Release boundary

A placement estimate, reduced-feature build, zero build ID, or
`--timing-allow-fail` route is diagnostic only. A usable release requires one
exact committed source revision and ROM identity to:

1. pass shared architecture, directed graphics, integrated graphics, boot, and
   SDRAM gates;
2. synthesize with zero SCCs and pass the enforced resource profile;
3. route every constrained clock with the complete feature set;
4. be packaged and flashed through NUC; and
5. pass repeated POST, SDRAM, kernel-entry, and HDMI checks on the board.

Record every structural route result immediately in `TIMING_CLOSURE.md`,
including the failed cone. A failed experiment with a measured reason is part
of the design record; an unrecorded seed hunt is lost work.
