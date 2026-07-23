# Astra 68 current engineering state

This is the short continuation map for the active machine. It records decisions
and validated boundaries that are easy to lose across long sessions. Detailed
contracts remain authoritative in the linked documents; historical handovers
and old resource tables are not current status.

The kernel's normative implementation contracts are
`KERNEL_ARCHITECTURE.md`, `MEMORY_MAP_AND_PMMU.md`, `ABI.md`,
`LOCKING_AND_PREEMPTION.md`, `RESOURCE_OWNERSHIP_AND_FAILURES.md`,
`MEMORY_BUDGET.md`, and `TEST_AND_FAULT_INJECTION_PLAN.md`; `STATUS.md`
separates implemented evidence from planned work.

## Locked architecture

- The sole RTL CPU is the repaired TG68K.C MC68030 integer core with integrated
  PMMU in `fpga/cpu/tg68k_c_030_mmu2`. There is no selectable fallback core,
  FPU, or cycle-accuracy requirement.
- Motorola MC68030 documentation is the architectural authority. Emulator,
  Amiga, MiSTer, upstream, and test behavior may expose a bug but never justify
  an Astra-specific CPU semantic. See [MC68030_COMPLIANCE.md](MC68030_COMPLIANCE.md).
- The core is a strong development baseline, not an unconditional production
  claim. SCC elimination, shared conformance, boot, SDRAM, and retained hardware
  tests pass substantial coverage. The previously open PMMU restart and
  fault-on-stacking cases now pass shared and focused RTL simulation. Exact
  candidate `F4DC1E18` is committed, fully simulated, timing-clean, and passes
  repeated SRAM-loaded board boots. Its hardware-profile coretest also passes
  translated reads/writes, invalid-descriptor fault recovery, and write
  protection on the ULX3S. A subsequent K-HW3 source delta now asserts RMC for
  the exact non-idle walker lifetime and passes wrapper, CDC, and adversarial
  SDRAM-arbiter simulation. A grouped K-HW4 delta also snapshots Vesta's IACK
  result for the full CPU transaction and gives timer restart/expiry races a
  deterministic contract with directed dual-simulator coverage. Neither delta
  is part of routed `F4DC1E18`, so a new full route and board qualification
  remain mandatory.
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
  historical NUC run passes all 90 unit tests, its 28-case shared matrix, and
  both Harte smoke targets.
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
- The shared Musashi/RTL matrix passes all 30 executions from an exact source
  snapshot, and both adapters pass the retained Harte smoke targets. Focused
  Vesta, AstraHost runtime/service, SDRAM bridge, ESP host, boot, NDK, and OSS
  release tests pass. This is enough for supervisor-mode kernel development,
  not a waiver for the remaining protected-multitasking DMA/IRQ dependencies.
- The 2026-07-22 K1 PMMU checkpoint closes the two exception/restart blockers
  in simulation. Separate-SRP/CRP `MOVES.B` absolute, postincrement, and
  predecrement writes plus a postincrement read each fault, enter vector 2,
  repair translation, return through an unmodified format-B `RTE`, update
  their address registers once, and perform one target transfer. A cold table
  walk for the translated supervisor exception stack also completes without a
  second fault or CPU halt. Beast passes 90 framework tests, all 30 shared
  Musashi/RTL executions, both Harte smoke targets, and the 137-variant Questa
  inventory at 111 clean, 18 classified failures, 3 stale compile failures,
  and 5 unscored diagnostics. The repaired source is committed as
  `5798c5575a5bf6d5ca37eae2fbe63cb0528ad6e8`. Exact production candidate
  `F4DC1E18` routes every constrained clock, and three independent SRAM reloads
  match its build and ROM identities while passing complete POST, 32 MiB BIST,
  DMA, runtime/input initialization, and `K0 ENTRY PASS`. This proves that the
  repaired netlist did not regress the nontranslated platform. A subsequent
  hardware-profile coretest passes with sum `74A6EC6D`, including PMMU register
  access, cold table walks, translated read/write, invalid-root access-fault
  recovery, and write-protection/no-write checks. Persistent hardware remains
  `6C0D0CA3`. The follow-on table-walk arbitration fix passes component and full
  coretest simulation but has not yet been synthesized, routed, or exercised
  on the board; HDMI and persistent-programming gates also remain.
- The follow-on K1 software checkpoint in independent Beast snapshot
  `/tmp/astra68-k1-p27` boots with the PMMU enabled and exercises production
  user-copy recovery rather than a synthetic handler. The kernel now has a
  bounded physical-frame allocator, owned DMA/block submission models, 4 KiB
  10/10/12 SRP/CRP tables, per-owner address spaces, guarded user-range checks,
  exact format-0/1/2/9/A/B exception decoding, and SFC/DFC `MOVES` user copies.
  Its startup check creates a real CRP, maps a process-owned page, verifies
  copy-in/copy-out, deliberately faults both read and write access to an
  unmapped page, returns through the vector-2 format-B fixup, and proves every
  temporary frame was reclaimed. The exact 29,140-byte boot binary
  (`382eddc628a7e2bebf416d31b9441f0ba2e7fc863e5a360dc96d12da6415fc4a`)
  and 17,860-byte kernel
  (`979347f21105a865243b5076b4ef9b04a9a3212fa386a2aebaf43f4177dae0ab`)
  reach `K0 ENTRY PASS` on both the full pin-level RTL/SDRAM model and AstraVM's
  vendored Musashi 68030/PMMU backend. Host unit tests, GCC `-fanalyzer`,
  ASan/UBSan, Rust unit tests, formatting, and Clippy `-D warnings` pass.
  This is simulation evidence only. Process/handle tables, trap ABI, saved
  contexts, user-mode entry, 100 Hz preemption, process-local fault death,
  teardown/soak, exact full routing, and board promotion remain K1 work.
- Independent Beast snapshot `/tmp/astra68-k1-p28` completes the K1 process
  path in simulation. It adds typed process handles, saved MC68030 contexts,
  the trap-based syscall ABI, two isolated ROM-packaged user programs, 100 Hz
  timer preemption, process-local fault death, and owner teardown. Bounded
  process-context maintenance now completes teardown that was deferred while
  device DMA was pinned without doing that work in the hard timer path, and the
  last process may exit or fault into an interruptible supervisor idle loop
  instead of causing a kernel panic. A fault
  found only after repeated trap/RTE and timer traffic exposed an RTL
  exception-entry defect: `arch_abort_pending` suppressed the A7 bank switch,
  so a user data-fault format-B frame was stacked on USP. The retained kernel
  fix permits only the exception-entry mode-switch write while an access fault
  is active and prevents a zero-wait fault response from being mistaken for a
  second stacking fault. Focused Questa runs pass with both inserted memory
  wait states and zero-wait responses. The supervisor tree now contains an
  exact unmapped stack guard; a deliberate access reaches the retained panic
  oracle with a format-A fault at `0x02028000`. Whole-space teardown invalidates
  caches before frame reuse, a fixed one-bit-per-frame ledger rejects duplicate
  cached user aliases, and two processes execute different code/data at the
  same logical addresses. The CACR command decoder now preserves independent
  instruction/data commands when both are written together. The production-form
  35,260-byte boot image
  (`7fa58266c26a3b3d679254235c3be2ad079f5f8710174940e6246e52e820022b`)
  runs unchanged on AstraVM/Musashi and the complete pin-level RTL/SDRAM model.
  Both enable the PMMU, start two user processes, preempt at 100 Hz, reap only
  the deliberate offender, reclaim its owned state, and reach
  `K1 PROTECTED ENTRY PASS` with scratch status `K1OK`. Host tests,
  `-fanalyzer`, ASan/UBSan, Rust tests, rustfmt, Clippy, all 90 shared framework
  tests, all 30 shared Musashi/RTL executions, both Harte smoke adapters, all
  directed graphics tests, and the 139-variant CPU inventory at 113 clean with
  the unchanged 3/18/5 classified buckets pass. A deliberate panic also reaches
  the console and retained early log in the full SoC model; both direct panic
  and exact supervisor-guard panic have independent full-SoC checks. The final
  committed normal image is 23,912 kernel bytes, runs unchanged on both models,
  and reports three context switches on each before `K1OK`. Source commit
  `66d6094f9339469313fefb70b259d07a7c2272ce` supplies the full ROM and kernel
  identity. Its exact full synthesis and placement now pass, but it still needs
  a completed strict route,
  repeated ULX3S qualification and long allocation, syscall, fault, and
  context-switch soaks before K1 release.
- Exact follow-on commit `470bf123cf24bbadf3525f91307e3d9aebe92006`
  adds the shared K1 lifecycle soak: each cycle faults and reaps one offender,
  verifies resource baselines, and relaunches it while the survivor remains
  schedulable. The immutable NUC snapshot has Git-archive SHA-256
  `b5db0e133ee04605fc1e18e4a159e1893893ca5c90c54df1c2ad8bcfc0c64fa5`.
  Host analyzer/sanitizer gates, an exact 100-cycle Musashi run, and the exact
  full pin-level RTL workload pass. RTL completes four post-milestone teardown
  cycles with 11 context switches, 23 timer ticks, 96 syscalls, and the exact
  7,987-free-page baseline. Independent release-duration Musashi runs on Beast
  and NUC both complete all 500,000 lifecycle cycles without drift. Beast
  reaches virtual cycle 1,252,807,889,504 after 12,416.334 seconds; NUC reaches
  virtual cycle 1,252,809,374,217 after 46,333.788 seconds with 1,000,001
  context switches, 2,022,386 timer ticks, a nonzero syscall total, and the
  exact 7,987-free-page baseline. Routed-hardware completion remains open and
  must not be inferred from simulation.
- Motorola-directed reset correction
  `c599f921cb35dcc7e8d2988ba253769341311516` separates ECP5 configuration
  initialization from MC68030 processor reset with one configuration-initialized
  bit. Its first released clock clears scalar PMMU state and invalidates the
  ATC; processor reset cannot re-arm it. `RESET` clears only TC/TT enable bits
  and preserves roots and valid ATC entries until software flushes them. The
  focused regression retains a deliberately stale ATC entry, checks
  CRP/SRP/control-field preservation, executes the same pre-enable `PFLUSHA`
  invariant used by K1 boot, and proves the next access walks to the changed
  descriptor. Beast's strict Questa inventory is 140 total and 114 clean; its
  prior 3 compile, 18 simulation, and 5 unscored classifications are unchanged.
  GHDL 7.0 generates the core without the aggregate-initializer crash or an ATC
  payload-wide startup mux. A byte-identical pre-commit reduced-BIST full
  pin-level SoC run passes POST, two-process 100 Hz preemption, offender-only
  fault containment, and four lifecycle cycles at the exact 7,987-page
  baseline. The already-
  running `66D6094F` route predates the correction and cannot be the release
  image; corrected exact synthesis, strict routing, and board reset/boot
  qualification are mandatory.
- Immutable corrected snapshot `77b3cdc8fddb984850073a2c2cb5998bbbe1d857`
  has archive SHA-256
  `678f4bb31a8c652615675b871274c992fde08d648a0e6f0a2e135361d168dbb5`.
  The exact normal image passes unchanged on Musashi and the complete
  pin-level RTL/SDRAM model. The latter runs full 32 MiB BIST at 115.06 MB/s,
  enables the PMMU, starts two isolated processes, preempts at 100 Hz, reaps
  only the deliberate offender, reports three context switches, and reaches
  `K1 PROTECTED ENTRY PASS`. All 90 framework tests, all 30 shared adapter
  executions, and both Harte smoke adapters pass. NUC
  full-chip synthesis reports zero SCCs, 53,073 LUT4s, 25,532 GSR-enabled FFs,
  101 block RAMs, and 18 multipliers. Exact seed-4 placement finishes normally,
  packing 66,513 TRELLIS_COMB and 25,561 TRELLIS_FF cells with checksum
  `0x7c9a8594`. Its uninterrupted no-waiver strict router1 route finishes
  normally with checksum `0x09264110`; every production clock passes, including
  14.179972 MHz CPU against 12.5 MHz and 61.270760 MHz SDRAM against
  60.002399 MHz. The `kernel_platform_v1` physical-capacity gate passes at
  66,513/83,640 TRELLIS_COMB, 101/208 block RAMs, and 18/156 multipliers. The
  production bitstream SHA-256 is
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`;
  manifest SHA-256 is
  `0593ba251da7b467e413126539d1e863ca19ef00f63843ed5f0cc6d32913b74e`.
  Exact direct-panic and supervisor-guard diagnostics also pass the full SoC
  model, with the latter faulting at `0x02028000`. Beast and NUC independently
  completed all 500,000 lifecycle-soak cycles without frame-count drift. The
  soak snapshot and routed candidate have identical `sw/kernel` and `sw/boot`
  sources. NUC atomically provisions only the exact
  ROM payload (`EB1B381F`) on the existing 244,016 MB card, restores normal
  read-only AstraHost firmware, and performs three independent SRAM reloads of
  the already-hashed bitstream. All three match build and ROM identities, pass
  complete POST and 32 MiB BIST, enable the PMMU, recover user-copy faults,
  preempt two isolated processes at 100 Hz, reap only the offender, and reach
  `K1 PROTECTED ENTRY PASS` in 2.127-2.147 seconds. Persistent FPGA flash still
  contains `6C0D0CA3`. The remaining release gates are physical HDMI
  confirmation, persistent reset/boot, physical panic diagnostics, and hardware
  soak; routing and SRAM success do not waive them.
- The one-shot physical diagnostics are prepared but have not been loaded.
  Their AstraHost application SHA-256 values are
  `1c579fa99a2041e82342839ac7f6372e11ccc896ed10e7dcb5ce2a5b07fc35fe`
  for direct panic,
  `6217a1b56163cbe78ab74d4c4e60da33725e2f585a929f5df6b86d654bb53067`
  for supervisor guard, and
  `5e3fc8691da085da408fa8baeb2548143a02b4c7de2c3c10986fb7bd4f13c7c9`
  for lifecycle soak. Independently extracted embedded packages have SHA-256
  values `2de9f718b8db67bbc5b015aae23f67bdf53cd65dc65f597bc76ac7314aca6635`,
  `bb0089aaf7f1248a74d3491e400bd8a383df548892fbc322add18a43cb309733`,
  and `cb55d88f5d16a9c2ec8e6548c051bb6ba96551b939643225574c56c969ad9c83`,
  with payload CRC32 values `FD4FC2AB`, `6AAAEE00`, and `B138EB36`,
  respectively. Reconstructing every package through `package_rom.py` proves
  its header CRC, payload CRC, size, load address, and reset vectors. The normal
  read-only AstraHost application remains
  `b4ec0fe43ffc7012758024576757df11892be0005e8e68fc282879448de962c2`
  and contains no provisioning package. The extracted direct-panic and guard
  packages are byte-identical to Beast's `k1_panic.rom` and `k1_guard.rom`
  artifacts used by the passing exact full-RTL diagnostics.
- Exact `F4DC1E18` canonical Beast mapping reports 52,943 LUT4s, 25,522
  synthesized FFs, 101 block RAMs, and 18 multipliers with zero SCCs. Its
  strict seed-4 heap/router1 route packs 66,377 TRELLIS_COMB cells, 25,555 FFs,
  101 block RAMs, and 18 multipliers. It passes at 14.015417 MHz CPU,
  66.423111 MHz SDRAM, 72.432274 MHz USB, 55.673088 MHz pixel, and
  307.125305 MHz HDMI shift. Bitstream SHA-256 is
  `bf6b86079227e042676ef495903162212a19092ab28fa83a7a09fbd261381d35`;
  routed-JSON SHA-256 is
  `cac12300397576b12ce9a386762d2d06c2f805ff6ebb98d1b36aa963f03a84e3`.
- The exact committed `6C0D0CA3` release synthesizes to 52,728 LUT4s and packs
  66,144 of 83,640 TRELLIS_COMB cells, 25,525 FFs, 101 block RAMs, and 18
  multipliers. Its strict router1 result passes every exact constraint at
  13.972139 MHz CPU and 63.403500 MHz SDRAM or better. Physical capacity, not
  an artificial utilization cap, is the release limit. See
  [FPGA_RESOURCE_BUDGET.md](FPGA_RESOURCE_BUDGET.md).
- A focused board diagnostic found one remaining hardware-only Astraea defect
  in that release: every multi-row blit was rejected with range error 1 while
  a one-row command passed. Zero-pitch and nonzero-pitch commands failed
  identically, and the CPU-visible command fields plus completion fences were
  correct. The failure is isolated to the unregistered DSP-backed
  `(height - 1) * pitch` range-validation path. The retained replacement uses
  a deterministic 16-step unsigned shift/add validator; directed graphics,
  integrated normal/INDEX8/RGB565 graphics, full CPU coretest, and complete
  HDMI-enabled AstraHost boot through `K0 ENTRY PASS` all pass. Canonical Beast
  synthesis has zero SCCs and maps 52,728 LUT4s, 25,492 FFs, 101 block RAMs,
  and 18 multipliers. Exact committed build `6C0D0CA3` routes the complete
  feature set at 13.972139 MHz CPU and 63.403500 MHz SDRAM, packing 66,144
  TRELLIS_COMB cells, 25,525 FFs, 101 block RAMs, and 18 multipliers. A
  route-preserving focused board image repeatedly passes all four one-row,
  multi-row, zero-pitch, and 720-byte-pitch commands; the complete graphics
  board image also reports `GFX PASS`. Four exact production reloads pass,
  including one after an independent AstraHost restart. Physical HDMI shows
  the exact kernel provenance, initialization results, `K0 ENTRY PASS`, and
  `KERNEL IDLE` with the corrected CP437 font.
- The 256 GB card and its existing GBA data are preserved. The one-shot
  maintenance firmware atomically replaced only `/ASTRA68.ROM` and reported
  the exact `6C0D0CA3` payload CRC32 `0fd82996`; normal AstraHost firmware is
  restored and mounts the FAT/exFAT boot volume read-only. It intentionally
  exposes no runtime media until the card has exactly one CRC-valid,
  non-overlapping Astra GPT partition. The bounded AstraHost input queue and
  the independent OHCI USB host are both integrated.
- Candidate promotion repeated that controlled procedure for `F4DC1E18`. The
  one-shot firmware mounted the existing 244,016 MB card without formatting
  and atomically replaced only `/ASTRA68.ROM` with the 17,652-byte payload,
  CRC32 `c7162f5a`, package SHA-256
  `58c29486d387fa8d8087252a1a238c4af766fe3144643e56c7b81a6b9299ccc6`.
  Exact normal read-only AstraHost firmware was restored and remounted the
  card. Three independent FPGA SRAM reloads then reached build `F4DC1E18`, ROM
  CRC32 `C7162F5A`, complete POST, full-range BIST, Astraea DMA, the 100 Hz
  timer, runtime/input initialization, and `K0 ENTRY PASS` in 1.615-1.627
  seconds. The card was then temporarily provisioned with the clean hardware
  coretest package (64,372 payload bytes, CRC32 `2FA3100C`); exact candidate
  `F4DC1E18` reported `CORETEST PASS sum=74A6EC6D` in 3.864 seconds. The
  production package and normal read-only AstraHost were restored immediately,
  and a final candidate reload again passed exact identity, complete POST,
  full-range BIST, DMA, and `K0 ENTRY PASS` in 1.616 seconds. Persistent FPGA
  flash is intentionally still `6C0D0CA3`. K-HW3 table-walk locking is now
  repaired and tested in source, but requires a new route and board pass along
  with physical HDMI confirmation. K-HW4 IACK/timer races are likewise closed
  in controller and focused CPU simulation: the selected vector cannot change
  in flight, spurious results remain spurious for that transaction, edge/level
  clearing is ordered, and one-shot restart cannot lose a simultaneous expiry.
  Hardware timer-race and interrupt-latency qualification remain open.
- Before the focused diagnostics, the ULX3S contained the exact `B1F9E60D`
  rollback release in volatile SRAM. Its bitstream SHA-256 is
  `05b9e84d2413c9390163a38f77c4d8ad08600a6adb619e69ebb25c56ae0e4eae`;
  its `/ASTRA68.ROM` SHA-256 was
  `2693a912e98a0fc1211b54b62dd80f8bed0544a3ac904d5b24d320c2be986423`.
  Three consecutive FPGA-only reloads reached exact build and ROM identity,
  complete POST, 32 MiB full-range BIST, and `K0 ENTRY PASS`. The board now
  contains exact production build `6C0D0CA3` in persistent FPGA flash. Its
  bitstream SHA-256 is
  `61538d09ef255b94206500185b31008fc242004ac954356365e0b9053c88e2d1`;
  `/ASTRA68.ROM` SHA-256 is
  `9daede67d0e4aa233018425a64060f09d2b045897c0d46fcf417831712dc7c6a`.
  Four reloads match both identities and pass complete POST, full-range BIST,
  Astraea DMA, timer/runtime/input initialization, and `K0 ENTRY PASS` in
  1.570-1.603 seconds. The fourth followed an independent AstraHost restart
  and proves normal SD remount plus FPGA SPI-link recovery. The identical
  bitstream was then programmed with `-f -r`; its reset-from-flash boot matched
  both identities and repeated complete POST, BIST, DMA, and kernel entry in
  1.630 seconds.
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
- Release promotion is complete for exact bitstream `6C0D0CA3`: strict route,
  focused and complete graphics diagnostics, SD ROM installation, four SRAM
  production boots, physical CP437 HDMI confirmation, persistent programming,
  and reset-from-flash boot all pass. Future releases must preserve the rule
  that no rebuild or repacking is permitted between SRAM acceptance and
  persistent programming.
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
