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

- The production clocks are 12.5 MHz CPU and 60 MHz SDRAM. The 60 MHz memory
  clock is an accepted architecture decision; HDMI remains 60 Hz, USB remains
  48 MHz, and the CPU clock is unchanged. Correctness and the retained feature
  set are not traded for routing.
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
  Mac has Homebrew `m68k-elf-gcc` 16.1.0. Shared Makefile detection selects
  `m68k-elf-` there and `m68k-linux-gnu-` on Linux while preserving explicit
  `CROSS=...` overrides. Beast's Linux cross compiler remains the canonical
  release compiler. The Mac's pinned Docker/OrbStack path is the
  verified NDK documentation builder; do not weaken the GCC analyzer or docs
  gate to fit the wrong host.
- OSS CAD revisions differ between hosts. A route made with another nextpnr or
  Yosys revision is useful diversity, not a controlled same-seed comparison.
  Record the exact host and tool identities with every retained artifact.
- Never clone a mutable build directory with hard links or `rsync --link-dest`.
  Yosys and ROM staging overwrite generated files in place and can silently
  mutate an earlier checkpoint. Snapshot sources independently and keep routed
  JSON artifacts immutable.
- Do not copy generated binaries between Mac and Linux work trees. A first NUC
  conformance attempt inherited current-looking Mach-O Musashi and GHDL
  executables from a Mac workspace and failed with `Exec format error`. Clean
  both targets or exclude build outputs before rebuilding natively; the clean
  NUC run passes all 90 unit tests, the 28-case shared matrix, and both Harte
  smoke targets.
- The checked-in ESP32 maintenance bridge is SRAM-only infrastructure. Board
  revision 3.0.8 uses the proven v3.1 ESP route. FPGA pin J3 is shared by ESP
  GPIO2 and SD D0/MISO, so the bridge may drive it low only while FTDI DTR
  requests download mode and must otherwise release it. A permanently driven
  low J3 permits ESP programming but breaks SD; an always-high-Z J3 permits SD
  but makes ESP flashing unreliable. See `fpga/maintenance/README.md`.

## Current integration state

- The minimum kernel-boot hardware is now implemented rather than stubbed:
  Vesta provides a 32-source programmable vectored interrupt controller and
  two timers; AstraHost provides queued runtime block I/O and normalized input
  events over SPI; an integrated OHCI controller provides one low/full-speed
  USB host port for keyboard and mouse; every platform IRQ is wired through
  Vesta to TG68K; and the kernel starts a checked 100 Hz timer before entering
  its idle loop.
- POST, front-panel MMIO, cache coherence, byte/address lanes, 32 MiB SDRAM
  BIST, Astraea DMA, SDRAM-backed ROM handoff, Vesta interrupt acknowledge,
  runtime block/input transport, and kernel entry pass focused and full
  pin-level simulation. The full boot reaches `K0 ENTRY PASS` and
  `KERNEL IDLE`; the retained 60 MHz BIST measures roughly 115 MB/s in
  simulation.
- Astraea and Vega retain the framebuffer, pixel-granular X/Y scrolling and
  wrap, 16 sprites, copper, blitter, line/rectangle/ellipse/flood/pattern
  drawing, shadow-scene presentation, and scanout paths. Tile layers are
  retired. At 60 MHz, directed tests pass and the integrated normal, INDEX8,
  and RGB565 workloads remain below the 1906-clock scanline deadline at maxima
  of 505, 1103, and 1429 clocks.
- The shared Musashi/RTL matrix passes all 28 cases from an exact clean source
  snapshot, and both adapters pass the retained Harte smoke targets. Focused
  Vesta, AstraHost runtime/service, SDRAM bridge, ESP host, boot, NDK, and OSS
  release tests pass. This is enough for supervisor-mode kernel development,
  not a waiver for the documented protected-multitasking PMMU blockers.
- The exact committed `B1F9E60D` release synthesizes to 52,565 LUT4s and packs
  66,093 of 83,640 TRELLIS_COMB cells, 25,449 FFs, 101 block RAMs, and 19
  multipliers. Its strict router1 result passes every exact constraint at
  13.646847 MHz CPU and 65.789474 MHz SDRAM or better. Physical capacity, not
  an artificial utilization cap, is the release limit. See
  [FPGA_RESOURCE_BUDGET.md](FPGA_RESOURCE_BUDGET.md).
- A focused board diagnostic found one remaining hardware-only Astraea defect
  in that release: every multi-row blit was rejected with range error 1 while
  a one-row command passed. Zero-pitch and nonzero-pitch commands failed
  identically, and the CPU-visible command fields plus completion fences were
  correct. The failure is isolated to the unregistered DSP-backed
  `(height - 1) * pitch` range-validation path. The candidate replacement uses
  a deterministic 16-step unsigned shift/add validator; directed graphics,
  integrated normal/INDEX8/RGB565 graphics, full CPU coretest, and complete
  HDMI-enabled AstraHost boot through `K0 ENTRY PASS` all pass. Canonical Beast
  synthesis has zero SCCs and maps 52,728 LUT4s, 25,492 FFs, 101 block RAMs,
  and 18 multipliers. It is not a release until an exact strict route and board
  validation pass.
- The 256 GB card and its existing GBA data are preserved. The one-shot
  maintenance firmware atomically replaced only `/ASTRA68.ROM` and reported
  the exact release payload CRC32 `ceafeee9`; normal AstraHost firmware is
  restored and mounts the FAT/exFAT boot volume read-only. It intentionally
  exposes no runtime media until the card has exactly one CRC-valid,
  non-overlapping Astra GPT partition. The bounded AstraHost input queue and
  the independent OHCI USB host are both integrated.
- The ULX3S currently contains the exact `B1F9E60D` release in volatile SRAM.
  Bitstream SHA-256 is
  `05b9e84d2413c9390163a38f77c4d8ad08600a6adb619e69ebb25c56ae0e4eae`;
  `/ASTRA68.ROM` SHA-256 is
  `2693a912e98a0fc1211b54b62dd80f8bed0544a3ac904d5b24d320c2be986423`.
  Three consecutive FPGA-only reloads reached exact build and ROM identity,
  complete POST, 32 MiB full-range BIST, and `K0 ENTRY PASS`. Persistent FPGA
  flash remains untouched pending physical confirmation of normal HDMI text.
- The legal full route and board-boot blocker is resolved. The corrected Beast
  router1 run refreshed all 66,566 placed combinational cells, completed after
  7,924.67 seconds, and passed the protected-LUT gate with zero violations. It
  meets every production clock at 14.544609 MHz CPU and 66.409882 MHz SDRAM,
  with all USB, SD, board, pixel, and HDMI-shift constraints also passing. The
  earlier route's 12,184 forbidden CCU2/distributed-RAM permutations remain a
  rejected historical checkpoint and explain its hardware-only CPU failure.
- P54 still passes the focused real-surface BERR/no-write test, full CPU
  coretest, full POST and kernel entry, directed graphics, and all integrated
  graphics modes with unchanged scanline maxima. Its mapped design has zero
  SCCs and GSR enabled on all 25,424 FFs. Those functional results remain
  necessary but do not waive the corrected physical-route gate.
- P55 removed the P54 route's 15.058 ns AstraHost boot/runtime ownership cone.
  A positive-control mapped-netlist check finds the old path and proves it is
  absent from P55. Focused AstraHost, complete boot, directed graphics, and all
  integrated graphics regressions pass at the locked 60 MHz SDRAM clock. The
  exact P55 route is timing-clean and repeatedly hardware-boots. Its four Y46
  font BRAM slices nevertheless read effective bank 3 while RTL selected bank
  0. Config-only all-ones, bytecode, bank marker, and CP437-copy experiments
  isolated that historical fault to font initialization/address handling
  rather than HDMI, text RAM, CPU writes, or Vega control.
- The source-level font correction is implemented and passes focused render,
  route-probe, and complete HDMI-enabled AstraHost boot simulations. Beast
  synthesis maps the exact 2 KiB CP437 image into one 2048x9 `DP16KD`, with all
  11 logical address bits connected, and reduces the complete design from 104
  to 101 block RAMs. The mapped checkpoint has zero SCCs, deterministic GSR on
  all 25,420 FFs, 52,565 LUT4s, 5,099 CCU2Cs, and 19 multipliers. The exact
  `B1F9E60D` route and three board reloads now pass; a mandatory mapped-netlist
  gate rejects multiple font blocks, constant logical address pins, or the
  wrong physical width.
- The active promotion gate is a strict full-feature route of the multi-row
  blitter correction, followed by a route-preserving focused diagnostic and
  repeated full POST, SDRAM, kernel-entry, graphics, and normal CP437 HDMI
  checks on ULX3S. Persistent FPGA flash remains untouched; no rebuild or
  repacking is permitted between SRAM acceptance and persistent programming.
- The canonical entry points agree on divider 0, 12.5 MHz CPU, 60 MHz SDRAM,
  heap timing weight 20, plain router1, and the measured critical floorplan.
  The split flow clears the placement-only waiver before release routing,
  checks every final clock, enforces resource policy, and binds stage 0 plus
  `/ASTRA68.ROM` in its manifest. Blind seed hunting remains unjustified; every
  retained route must record its exact cone in
  [TIMING_CLOSURE.md](../fpga/soc/oss_flow/TIMING_CLOSURE.md).

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
