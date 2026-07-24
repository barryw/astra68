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

- The active K4 synchronization checkpoint is qualified from exact source
  `662aa04ef807d6c74ea1a8d0c3a95b8eb78931e7`; the implementation landed in
  `4a878c9095213d9009e3ad6eeca85ebac3d7c936` and the later source delta adds
  exact K4 RTL and hardware acceptance only. The immutable qualification
  archive SHA-256 is
  `ee06971a5239d890ea9e157ae46638bdefa94d3eb86c13656e1aaeedf00d6fe3`.
  K4 replaces the private event
  qualifier with public generation-safe event and semaphore handles carrying
  explicit wait/signal rights. Waits use absolute monotonic nanosecond
  deadlines and arbitrate signal, timeout, cancellation, close, and owner death
  exactly once through the existing per-thread wait link and 16-entry deadline
  heap. The implementation has 32 fixed 36-byte objects, an eight-object owner
  quota, at most 16 global waiters, no wait-path allocation, priority/FIFO wake
  ordering, and immediate higher-priority handoff.

  All 17 host suites pass normally, under GCC `-fanalyzer`, and under
  ASan/UBSan/leak checks. NDK host, sanitizer, analyzer, m68k library/example,
  and canonical kernel/ROM verification gates pass. The exact normal Musashi
  boot reaches every K1-K4 marker in 19,000,216 virtual cycles. The exact
  1,000-cycle workload finishes in 622,507,501 cycles under the unchanged
  675,000,000 cap, retains the 7,986-page baseline, and reports zero hot-path
  overruns. Exact hardware-profile artifacts are a 44,740-byte kernel with
  SHA-256
  `11c2ed31ca5caf07dcfbd87cf354f6ce7be3eb1873cef412b65a6821940fb91c`
  and a 56,152-byte boot payload with CRC32 `2F9B149C` and SHA-256
  `15f713f45e5e8b1eec1bf9820759e915186b70339d3704c0e0771d35df47e588`;
  the 56,184-byte ROM
  package SHA-256 is
  `14f4f980ebeb3fed099ac44d3035e6a1d6f1b2aa354b87bd208c468eb1b66c28`.

  The exact pin-level RTL/SDRAM model passes the intentional 64 KiB simulated
  BIST and every K1-K4 marker in 266.959 seconds. It reports the exact K4
  lifecycle counts, a 6,164/20,000-cycle deadline maximum, and zero overruns.
  NUC preserved the existing 244,016 MB card, replaced only `/ASTRA68.ROM`,
  independently verified the installed file, and restored read-only
  AstraHost. Two independent volatile loads of production FPGA build
  `25D9CB8E` pass exact ROM CRC32 `2F9B149C`, real 32 MiB BIST, PMMU/user-copy
  isolation, all K4 lifecycle and handoff checks, every K1-K4 marker, and every
  performance gate with zero overruns. K4 is now the hardware-qualified kernel
  checkpoint; K3 remains its rollback point. This software-only promotion did
  not synthesize, place, route, pack, or rewrite FPGA flash, so resources,
  clocks, and persistent FPGA build `25D9CB8E` are unchanged.
- The hardware-qualified K3 predecessor is represented on `main` by
  `3787d820e1140f49ba31623ccc578bb274a631cc`. Its retained target artifact was
  developed from `8929c063cdd24c8f4f526be330549e2eb5038fc8-dirty`, retains the K2
  process/thread split and adds exact 5 ms one-shot quanta plus one fixed
  16-entry deadline heap. Vesta is programmed to the earlier of the running
  thread's 62,500-cycle quantum and earliest absolute wait deadline. Ordinary
  syscalls do not renew the quantum. Timeout, signal, close, and process death
  withdraw a waiter exactly once; a higher-priority signal or timeout preempts
  immediately. All-blocked operation retains the deadline while the guarded
  supervisor worker idles. No timer or deadline path allocates.

  Seventeen host suites, GCC `-fanalyzer`, ASan/UBSan/leak checks, canonical
  m68k verification, every Rust gate, all 90 framework tests, all 30 shared
  executions, both Harte smoke adapters, and the directed Vesta timer/IACK race
  test pass. Exact normal Musashi reaches K3 in 17,250,229 cycles. The exact
  1,000-cycle workload completes in 596,507,297 cycles against the unchanged
  675,000,000 cap, 49,251,790 cycles faster than K2. The kernel is 41,020 bytes
  with SHA-256
  `6ab38364d2ef5e67b6f5e8c7fb691cbf45291624562d7a0203f812c2e648e61d`.
  The 52,444-byte HDMI payload has CRC32 `BAEF4D0B`; the 52,476-byte package
  SHA-256 is
  `b73964d87904994a570c3b5e2b931602f8eb7878f0b531c0ac7e775050919ab1`.

  The complete pin-level model passes with an intentional 64 KiB simulated
  BIST and a 6,163/20,000-cycle deadline maximum. NUC preserved the existing
  card, changed only `/ASTRA68.ROM`, independently verified the installed file,
  and restored read-only AstraHost. Two independent loads of exact production
  bitstream `25D9CB8E` pass the real 32 MiB BIST, PMMU isolation, one-shot
  preemption, timeout handoff, all K1/K2/K3 markers, and every performance gate;
  each measures 6,177/20,000 deadline cycles with zero overruns. This is a
  software-only checkpoint: persistent FPGA flash remains `25D9CB8E`, and no
  synthesis, placement, route, timing, or utilization result changes. NUC has
  no HDMI capture device; the unchanged HDMI pipeline retains exact physical
  K1 qualification, while a K3 screen photograph remains visual evidence only.
- The minimum kernel-boot hardware is now implemented rather than stubbed:
  Vesta provides a 32-source programmable vectored interrupt controller and
  two timers; AstraHost provides queued runtime block I/O and normalized input
  events over SPI; an integrated OHCI controller provides one low/full-speed
  USB host port for keyboard and mouse; every platform IRQ is wired through
  Vesta to TG68K; and the kernel starts a checked 5 ms one-shot scheduler timer
  before entering user mode.
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
  `K1 PROTECTED ENTRY PASS` in 2.127-2.147 seconds. Physical HDMI then shows
  exact build `77B3CDC8`, the complete PMMU/process/fault result, and
  `K1 PROTECTED ENTRY PASS`; the retained screenshot is
  `docs/evidence/k1-77b3cdc8-sram-hdmi.png`, SHA-256
  `8b6d0d57bf7f029aa63506c348976830079ccabcdeb6a3cf38cad51d3365b051`.
  NUC programmed that already-hashed bitstream into FPGA flash and reset the
  board in the same operation. The automatic flash boot again matches build
  `77B3CDC8` and ROM CRC32 `EB1B381F`, passes the complete hardware gate, and
  reaches K1 entry in 2.132 seconds. Its retained log is
  `docs/evidence/k1-77b3cdc8-flash-reset.log`, SHA-256
  `deeaba2d4acdb5fbc5115085b4f751796ce11079cc68ded319c43117d17b0e97`.
  At that checkpoint, persistent FPGA flash contained exact K1 candidate
  `77B3CDC8`. The
  physical direct-panic and supervisor-guard diagnostics now also pass.
  At that checkpoint hardware lifecycle qualification was partial: 1,000
  physical teardown cycles retained the exact resource baseline, while bounded
  interrupt latency and the time-boxed mixed release burn-in remained open.
  Routing, normal boot,
  persistent promotion, and panic qualification do not waive those gates.
- The one-shot physical diagnostic identities are pinned before use.
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
  artifacts used by the passing exact full-RTL diagnostics. NUC provisioned the
  direct-panic package without formatting the existing card, reporting exact
  35,040-byte payload CRC32 `FD4FC2AB`; provisioner-log SHA-256 is
  `0945eadf79a820ed5fbfa87075877a648dd9c4a465721bf4c5b15748ed66020f`.
  After normal read-only AstraHost was restored, the unchanged production
  bitstream passed complete POST and reached the ordered deliberate panic plus
  `SYSTEM HALTED` in 1.816 seconds; checker-log SHA-256 is
  `e6297e0b7adb8e2cc0352fc1c6575d6c02dbc09312059ee66ca1206ad5b8114a`.
  Physical HDMI shows the same panic, exact Git and build identities, and halt;
  `docs/evidence/k1-77b3cdc8-direct-panic-hdmi.png` has SHA-256
  `639785017f2691b7e4cebc493289f0e0f15d89762aed34c4994c869bce17a8de`.
  NUC next provisioned the exact 35,112-byte supervisor-guard payload at CRC32
  `6AAAEE00`; provisioner-log SHA-256 is
  `1584d6dbee2fcf0c4c903f0d7dd3cc0ccdaed66d34dc3e1e4110a2b81dfc78be`.
  With normal AstraHost restored, the same bitstream passes complete POST and
  reports a format-A vector-2 exception, SSW `0x0105`, and exact guard address
  `0x02028000` before `SYSTEM HALTED` in 1.821 seconds. Checker-log SHA-256 is
  `01aa5fd5d578ad94291a82f9f771df89395274c0ac7a9a42cf702784d9abc0d0`.
  Physical HDMI independently shows every field;
  `docs/evidence/k1-77b3cdc8-guard-hdmi.png` has SHA-256
  `d7289448fb1453fee1e6be617eaad00d458d267f68183416f83ebfa1a827dce1`.
  NUC provisioned the
  exact 36,288-byte soak payload at CRC32 `B138EB36`; provisioning-log SHA-256
  is `483f77b140d083cf5658fc076240d88fa99aaad9764c08e6d2477f454f5e3cde`.
  A 100-cycle hardware qualification reaches K1 entry and reports cycles 4,
  10, and 100 at the unchanged 7,987-page baseline in 29.440 seconds; its log
  SHA-256 is
  `59cb09b9a8a0b4b253d9ae8cd661718c82bead9d7bcbdf7568f6f8ced9cfeb27`.
  That run exposed a host-checker partial-line match, not a kernel failure:
  commit `a363c7c` requires a terminated checkpoint and exact equality with the
  announced baseline, while `254d0f6` streams each complete UART line durably.
  All 21 boot-tool tests pass. A planned 500,000-cycle run started on NUC at
  2026-07-23 15:35:12 EDT as user service `astra-k1-soak-500k`, invocation
  `e03e0b123fd548eca5d5892cc5c74aef`, using checker SHA-256
  `7ab14afacde4cb80fe90d35045d3966b15779b18c9fec1953ad139658fae0784`.
  It was stopped intentionally after preserving complete cycles
  4, 10, 100, and 1,000 at exactly 7,987 free pages. Cycle 1,000 reports 2,003
  switches, 5,536 ticks, and syscall count `0x5717`; retained snapshot
  `docs/evidence/k1-77b3cdc8-soak-1000-hw.log` has SHA-256
  `4229a2e698707d4892d5e13797a496596f426ae8bd5457586135ae77a667893b`.
  Source inspection explains the roughly 0.29-second lifecycle: the user-fault
  path runs at IPL 7, scans a 1,024-entry root plus populated 1,024-entry page
  tables, poisons whole 4 KiB pages, and makes two full 8,192-frame owner scans.
  Vesta has one pending expiration bit, so masked 10 ms periods coalesce and
  the 5,536 count measures delivered interrupts rather than elapsed 100 Hz
  periods. Exhaustive 500,000-cycle coverage remains on independent Musashi
  hosts; physical qualification uses bounded candidate and 30-minute mixed
  burn-in gates.

  NUC then atomically restored only the exact normal payload CRC32 `EB1B381F`,
  normal read-only AstraHost application SHA-256
  `b4ec0fe43ffc7012758024576757df11892be0005e8e68fc282879448de962c2`,
  and production bitstream SHA-256
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`.
  The complete normal gate passes again in 2.111 seconds. Retained log
  `docs/evidence/k1-77b3cdc8-normal-restored-after-soak.log` has SHA-256
  `4505cb1b81c6b030df02d7ddf1997c16b532ecdb44c43f35403142da8413a150`.
  At that checkpoint the board and FTDI port were available, and persistent
  FPGA flash remained exact `77B3CDC8`.
- Exact software follow-on `bbb1616a1e65ef56619bffb11cb21e9ea1bc5202`
  closes the diagnosed lifecycle latency path without changing RTL or the
  production bitstream. User-fault dispatch now marks the offender `EXITING`,
  switches to a runnable or empty CRP, and queues teardown; address-space
  destruction, handle close, page poisoning, and frame release run later with
  interrupts enabled. The allocator uses 64 fixed owner ledgers and two 16-bit
  links per physical frame, making owner release O(frames owned) instead of two
  scans over all 8,192 frames. Pinned release is atomic, ledger exhaustion is a
  pre-publication allocation failure, and empty ledgers are reusable. The
  33,280-byte static cost moves normal `.noinit` to 102,016 bytes while keeping
  `_kernel_memory_end` at `0x02034000` inside the 512 KiB reservation.

  Beast passes all 11 kernel suites, GCC `-fanalyzer`, ASan/UBSan/leak checks,
  the canonical m68k compiler gate, all 90 shared framework tests, the complete
  30-execution Musashi/RTL matrix, and both Harte smoke adapters. The optimized
  100-cycle Musashi run reaches virtual cycle 77,501,092 in 0.814 seconds with
  a 4,482-cycle masked-fault maximum. The full pin-level SoC/SDRAM model
  completes 13 teardown cycles in 355.123 seconds with an 8,866-cycle maximum
  and the exact 7,987-page baseline.

  NUC atomically installed exact soak package SHA-256
  `a8acc504ac7b58e19896b3811533e6c31ea795843d5f6ad272f3786887b6ebf4`
  (payload CRC32 `18776505`) while preserving the 244,016 MB card, restored the
  known read-only AstraHost application, and loaded unchanged production
  bitstream SHA-256
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`.
  Hardware reaches cycle 100 in 8.219 seconds with 205 switches, 636 delivered
  ticks, syscall count `0x689`, exactly 7,987 free pages, and an 8,834-cycle
  (706.72 us) masked-fault maximum against the 125,000-cycle gate. Retained log
  `docs/evidence/k1-77b3cdc8-bbb1616-soak-100-hw.log` has SHA-256
  `928fdd5414aacb237c5818293a464c3860ffcd0c7cf6d0a48f2fbcf200f0fb5e`.

  NUC then atomically restored exact normal package SHA-256
  `262130dcb7880f8f72ae2da96a9ad3dd2806a6b51b129c78df3991757eb0c23d`
  (payload CRC32 `C030B951`), restored the same read-only AstraHost application,
  and reloaded the same production bitstream. Complete POST, PMMU/user-copy,
  process isolation, offender-only fault containment, and
  `K1 PROTECTED ENTRY PASS` complete in 1.931 seconds. Retained log
  `docs/evidence/k1-77b3cdc8-bbb1616-normal-hw.log` has SHA-256
  `6197aeeeb3a55ea1d8366025a6c64f9d2a424b17791bb60d3ab72d8ec6916b86`.
  The board was left in that normal state. No synthesis, placement, route,
  resource, or constrained-clock result changed. At this checkpoint the
  30-minute mixed hardware burn-in was the next K1 release gate.
- Exact hardware-qualification follow-on
  `853ae66e300232dcbdf5f69903747faa42521114`, archive SHA-256
  `c416c1bfb0720ac2bf9fb94898a99e66e1e7b6040f215706a661462ea493f7ad`,
  adds a coherent 64-bit FPGA cycle-counter snapshot and binds elapsed-time
  acceptance to complete K1 checkpoints. The kernel reads the low word first
  to latch the counter, then the high word; the soak computes wrap-safe elapsed
  cycles without a compiler runtime helper. The checker rejects partial lines,
  counter regressions, baseline drift, zero activity, excessive latency, and
  checkpoints below the requested elapsed-cycle threshold. All 11 kernel
  suites pass normally, under GCC `-fanalyzer`, and with ASan/UBSan/leak
  checks; all 22 boot-tool tests, 15 Rust tests, rustfmt, Clippy, the exact
  m68k builds, and a 1,000-cycle Musashi run pass.

  NUC mounted the existing 244,016 MB card without formatting and atomically
  replaced only `/ASTRA68.ROM` with soak package SHA-256
  `9f6953911f10d726d51b861eeb5d42a4a54c15841e385125ecbd7f1257b8ab53`,
  37,384-byte payload CRC32 `91E30139`. A second provisioner boot verified the
  file already matched; retained log
  `docs/evidence/k1-77b3cdc8-853ae66-soak-provision.log` has SHA-256
  `28815aa647d9aacb15f8c93bde6df97e1ef169ce3e0874f0c2cf8d5e8189ad75`.
  With normal read-only AstraHost restored and unchanged production bitstream
  SHA-256
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`,
  the routed candidate gate passes at cycle 5,000 after 317.246 host seconds
  and `0x00000000EAE8411F` FPGA CPU cycles. It reports 10,005 switches, 31,533
  delivered ticks, syscall count `0x15288`, exactly 7,987 free pages, and an
  8,809-cycle masked-fault maximum. Retained log
  `docs/evidence/k1-77b3cdc8-853ae66-candidate-5m-hw.log` has SHA-256
  `db9ad4900951e3cc61ae20d8078bd714a20089bcd1880f0f77dc58d34f64dbf6`.

  A separate reset then passes the release burn-in at cycle 29,000 after
  1,830.658 host seconds and `0x000000055263857F` coherent FPGA CPU cycles,
  exceeding both the 30-minute `0x000000053D1AC100` threshold and 5,000-cycle
  minimum. It reports 58,005 switches, 182,861 delivered ticks, syscall count
  `0x7AD6B`, exactly 7,987 free pages, and the unchanged 8,809-cycle maximum.
  Retained log `docs/evidence/k1-77b3cdc8-853ae66-release-30m-hw.log` has
  SHA-256
  `71d2c3a766bc1cd25a58f6e81ca9c904517b0df74322d2d3130279a0e1ffa489`.

  NUC finally restored normal package SHA-256
  `696afc6ecf9d5df31acc76966aeea0fe190b44479c4af61a2fbf16f8866f7d05`,
  36,292-byte payload CRC32 `BBAB0AA1`, and verified an exact second-boot match.
  Provisioning-log SHA-256 is
  `5be77adb8627fbd0ca4a6ede6601c92d362d44bf0394dce3f31fad8f0c398929`.
  Read-only AstraHost application SHA-256
  `b4ec0fe43ffc7012758024576757df11892be0005e8e68fc282879448de962c2`
  was restored before the same production bitstream was loaded. The final boot
  reports exact build `77B3CDC8`, ROM CRC32 `BBAB0AA1`, full Git identity
  `853ae66e300232dcbdf5f69903747faa42521114`, complete POST/BIST, PMMU and
  fault containment, and `K1 PROTECTED ENTRY PASS` in 1.955 seconds. Retained
  log `docs/evidence/k1-77b3cdc8-853ae66-normal-hw.log` has SHA-256
  `14b69338b1c429def6fa0a13067bff6e00f087dae0dc7a05a3a463e7a107f09c`.
  At that checkpoint the board was left in that normal state; persistent FPGA
  flash remained the same exact `77B3CDC8` image. No RTL, synthesis,
  placement, route, resource, or constrained-clock result changed during this
  qualification.
- CPU correction `9a977e13f560b4c85eafc7835d88aad437314491` and guarded
  worker `42f4bb55ebd5ac47d057162322e293e4999a2661` form the current exact
  guarded-worker release. The CPU now preserves M in the MC68030 section 8.1.9
  format-1 throwaway frame, avoids a second MSP postadd, and settles
  restored-PC fetch before chained `RTE` retirement. The kernel runs process
  reclamation with interrupts enabled on a dedicated guarded 8 KiB MSP;
  exception and IRQ entry
  retain the separate guarded 8 KiB ISP. Work and retry queues are each one
  bounded bit, and panic output includes exact worker/maintenance state.

  Beast passes 12 kernel suites normally, with GCC `-fanalyzer`, and with
  ASan/UBSan/leak checks; canonical m68k verification, 15 Rust tests, rustfmt,
  Clippy, all 90 framework tests, all 30 shared executions, and both Harte
  smoke adapters pass. The complete strict Questa inventory is 141 total, 115
  clean, and the existing 3 compile, 18 simulation, and 5 unscored buckets are
  unchanged. Musashi reaches normal K1 and completes 1,000 lifecycle cycles at
  virtual cycle 640,260,129 with 2,001 switches and 7,987 free pages. The
  complete pin-level RTL/SDRAM model reaches normal K1 in 130.017 seconds and a
  stable worker soak checkpoint in 191.959 seconds, both with 115.03 MB/s BIST.
  Exact full Beast synthesis has zero SCCs and maps 53,079 LUT4s, 25,536
  GSR-enabled FFs, 101 block RAMs, and 18 multipliers. The exact no-waiver
  seed-4 heap/router1 route packs 66,523 TRELLIS_COMB and 25,565 FFs and passes
  at 15.058201 MHz CPU, 66.907532 MHz SDRAM, 79.693970 MHz USB, 53.267990 MHz
  pixel, and 289.771088 MHz HDMI shift. Exact build `25D9CB8E` bitstream
  SHA-256 is
  `78cd218f12feb72ccbdcb6bb141d19908c961f3438b6b559bf99b60d1c9d6940`;
  normal ROM CRC32 is `D21EF603`.

  NUC mounted the existing 244,016 MB card without formatting and changed only
  `/ASTRA68.ROM`. Three independent SRAM loads pass exact build/ROM identity,
  complete POST and 32 MiB BIST, PMMU/user-copy checks, the guarded MSP worker,
  100 Hz preemption, offender-only fault containment, and K1 protected entry.
  The exact soak ROM then passes 5,000 worker/fault cycles over 302.531 host
  seconds and `0x00000000DFEAD7D7` CPU cycles with 10,003 switches, 30,057
  delivered ticks, 48,709 syscalls, an unchanged 7,987-page baseline, and a
  9,376-cycle maximum masked-fault interval. Normal ROM and read-only AstraHost
  were restored before a fourth SRAM boot passed. Exact release
  `25D9CB8E` was then written to FPGA flash; automatic reset-from-flash passed
  the same normal gate in 2.008 seconds. Persistent hardware now contains
  `25D9CB8E`. Physical HDMI visibly reports the full `e108a371...` Git
  identity, guarded MSP worker, PMMU, preemption, fault containment, and
  `K1 PROTECTED ENTRY PASS`. Retained screenshot
  `docs/evidence/k1-25d9cb8e-e108a37-flash-hdmi.png` has SHA-256
  `e6e654d6ad0c9f5dead16f9116ab622d7a5ba731fc2fafc1ff7ba324c08128a4`.
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
  full-range BIST, DMA, and `K0 ENTRY PASS` in 1.616 seconds. At that
  checkpoint persistent FPGA flash was intentionally still `6C0D0CA3`.
  K-HW3 table-walk locking was repaired and tested in source, but required a
  new route and board pass along with physical HDMI confirmation. K-HW4
  IACK/timer races were likewise closed in controller and focused CPU
  simulation: the selected vector could not change
  in flight, spurious results remain spurious for that transaction, edge/level
  clearing is ordered, and one-shot restart cannot lose a simultaneous expiry.
  Hardware timer-race and interrupt-latency qualification remained open.
- Before the focused diagnostics, the ULX3S contained the exact `B1F9E60D`
  rollback release in volatile SRAM. Its bitstream SHA-256 is
  `05b9e84d2413c9390163a38f77c4d8ad08600a6adb619e69ebb25c56ae0e4eae`;
  its `/ASTRA68.ROM` SHA-256 was
  `2693a912e98a0fc1211b54b62dd80f8bed0544a3ac904d5b24d320c2be986423`.
  Three consecutive FPGA-only reloads reached exact build and ROM identity,
  complete POST, 32 MiB full-range BIST, and `K0 ENTRY PASS`. At that
  checkpoint the board contained exact production build `6C0D0CA3` in
  persistent FPGA flash. Its
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
