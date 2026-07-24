# Astra 68 kernel status

Status date: 2026-07-24

This is the kernel-specific truth table. `CURRENT_STATE.md` remains the whole
machine continuation map. A row marked CURRENT has evidence; PLANNED or MISSING
must not be presented as working software.

## Current source identity

- Branch: `main`.
- K1 functional source commit: `66d6094f9339469313fefb70b259d07a7c2272ce`.
- K1 lifecycle-soak source commit:
  `470bf123cf24bbadf3525f91307e3d9aebe92006`.
- PMMU processor-reset conformance source commit:
  `c599f921cb35dcc7e8d2988ba253769341311516`.
- Exact corrected qualification snapshot: commit
  `77b3cdc8fddb984850073a2c2cb5998bbbe1d857`, archive SHA-256
  `678f4bb31a8c652615675b871274c992fde08d648a0e6f0a2e135361d168dbb5`,
  extracted as `/tmp/astra68-k1-reset-77b3cdc` on NUC and Beast.
- Deferred-reclamation source commit:
  `bbb1616a1e65ef56619bffb11cb21e9ea1bc5202`, archive SHA-256
  `0e2a64c37871bfc70601e363da72437e821437e6efad3915ef180fe5cf1e9d50`,
  extracted as `/tmp/astra68-k1-bbb1616` on NUC. It changes ROM/kernel
  software and acceptance tooling only; production bitstream `77B3CDC8` is
  unchanged.
- Hardware-burn-in source commit:
  `853ae66e300232dcbdf5f69903747faa42521114`, archive SHA-256
  `c416c1bfb0720ac2bf9fb94898a99e66e1e7b6040f215706a661462ea493f7ad`,
  extracted as `/tmp/astra68-k1-853ae66` on NUC and Beast. It adds coherent
  64-bit elapsed-cycle reporting and acceptance checks only; production
  bitstream `77B3CDC8` remains unchanged.
- Master-mode interrupt correction commit:
  `9a977e13f560b4c85eafc7835d88aad437314491`. It fixes the exact
  format-1/format-0 chained `RTE` path and adds the Motorola-directed M=1
  dual-frame regression.
- Guarded deferred-worker commit:
  `42f4bb55ebd5ac47d057162322e293e4999a2661`. Exact committed release source
  `e108a3711befa08a309f068939dff226a21c869c` has archive SHA-256
  `52420b817dd77be3632640b34ea7a5e2136ededec5c59970c93f883c559ef395`
  and was extracted independently on NUC and Beast. Host, Musashi, focused and
  complete RTL, exact route, repeated ULX3S boot, bounded soak, and
  reset-from-flash gates pass.
- Immutable build snapshot: Beast `/tmp/astra68-k1-66d6094` from Git archive
  SHA-256 `ba4d91999cf829c33a345d895b7966a438b28b93871d8d34843d658a1d0c0039`.
- Immutable soak snapshot: NUC `/tmp/astra68-k1-soak-470bf12` and independent
  Beast `/tmp/astra68-k1-soak-beast` from Git archive SHA-256
  `b5db0e133ee04605fc1e18e4a159e1893893ca5c90c54df1c2ad8bcfc0c64fa5`.
- The qualified normal, direct-panic, and guard-panic ROMs report that complete
  Git identity and reproducible commit timestamp.

## Works now

| Mechanism | State | Evidence |
|---|---|---|
| BootInfo validation and separate kernel image | CURRENT | host, Musashi, full RTL |
| kernel VBR, guarded 8 KiB ISP, guarded 8 KiB worker MSP | CURRENT HW | host descriptors, Musashi, focused/full RTL, exact ULX3S boot and soak |
| frame allocator for all 8,192 4 KiB frames | CURRENT HW | host tests, target startup, 64-owner bounded ledger, exact release-visit accounting |
| SRP/CRP 4 KiB two-level translation | CURRENT | host, focused RTL, Musashi, full RTL |
| PMMU enable and CRP switching | CURRENT HW | Musashi, full RTL, and three exact SRAM K1 boots |
| user null/code/stack guards | CURRENT | host mapping tests and target fault |
| supervisor stack guard | CURRENT HW | exact descriptor host test plus deliberate format-A vector-2 guard panic at `0x02028000` in full RTL and physical hardware |
| cross-CRP cache isolation | CURRENT SIM | distinct same-address code and stack markers on Musashi/full RTL |
| duplicate cached user-alias rejection | CURRENT HOST | one-bit/frame VM ledger and remap test |
| whole-address-space cache invalidation | CURRENT HOST | destruction invalidates before descriptor removal/frame reuse |
| format 0/1/2/9/A/B frame decode | CURRENT | byte-exact host tests |
| SFC/DFC copyin/copyout fault recovery | CURRENT | focused RTL, Musashi, full RTL |
| typed generation handle table | CURRENT | host tests; 16 entries/process in K1 |
| process creation and owner teardown | CURRENT HW | host, Musashi, full RTL, and exact 100-cycle ULX3S fault/reap path |
| deferred pinned-DMA reap | CURRENT SIM | guarded worker state machine on host; Musashi and full RTL lifecycle soaks |
| two isolated user processes | CURRENT HW | same ROM on Musashi, full RTL, and three exact SRAM boots |
| 100 Hz preemptive round-robin | CURRENT HW | three switches before K1 marker on Musashi, full RTL, and each exact SRAM boot |
| trap ABI query/progress/yield/exit/close | CURRENT PROVISIONAL | host and target K1 |
| offender-only user fault death | CURRENT HW | format-B fault reaps only the offender on Musashi, full RTL, and three exact SRAM boots |
| last-process supervisor idle transition | CURRENT HOST | process/dispatch tests; target assembly builds |
| panic to console and retained early log | CURRENT HW | exact direct and supervisor-guard panic paths pass full RTL plus physical HDMI/log qualification |
| K1 host analyzer/sanitizer gates | CURRENT | 12 suites, analyzer, ASan/UBSan |
| deterministic lifecycle-soak harness | CURRENT HW | dual-host 500,000-cycle legacy Musashi, optimized Musashi, 13-cycle full RTL, routed five-minute candidate, and independent 30-minute release runs pass without drift |
| deferred user-fault reclamation | CURRENT HW | host proves no maintenance/owner release in fault dispatch; Musashi, full RTL, and ULX3S report bounded masked-fault cycles |
| shared CPU/PMMU framework | CURRENT | 90 tests, 30 adapter executions, Harte smoke |
| master-mode interrupt dual-frame return | CURRENT RTL | exact Motorola format-0/1 frames, MSP chain, multiword restart, complete strict inventory |
| fixed interruptible process-reap worker | CURRENT HW | guarded MSP/ISP, coalesced work bit, bounded retry, normal and soak target images, exact five-minute ULX3S run |
| CACR independent I/D commands | CURRENT RTL | Motorola-directed mixed CI/CD decoder test; strict inventory 141/115 clean |
| RESET preserves roots and ATC until explicit flush | CURRENT RTL/ROUTED | stale-ATC/reset/`PFLUSHA` regression; strict inventory 141/115 clean; prior exact full mapping has zero SCCs and passes all clocks |
| prior corrected K1 release ROM | CURRENT | build `77B3CDC8` remains a qualified rollback artifact with Musashi/full RTL and routed-hardware evidence |
| guarded-worker K1 release ROM | CURRENT HW | build `25D9CB8E` passes exact route, repeated SRAM boots, five-minute worker soak, and reset-from-flash |
| guarded-worker K1 hardware boot | CURRENT HW | exact identity, full POST/BIST, PMMU, guarded worker, 100 Hz preemption, offender-only fault containment, K1 entry, and physical HDMI pass |

## Hardware status

- The ULX3S attached to NUC now runs exact persistent guarded-worker release
  `25D9CB8E`. Prior `77B3CDC8` K1 and `6C0D0CA3` K0 images remain qualified
  rollback artifacts, not the board's current flash contents.
- Exact `25D9CB8E` maps 53,079 LUT4s, 25,536 GSR-enabled FFs, 101 DP16KDs,
  and 18 multipliers with zero SCCs. The no-waiver route packs 66,523
  TRELLIS_COMB and 25,565 FFs and passes at 15.058201 MHz CPU, 66.907532 MHz
  SDRAM, 79.693970 MHz USB, 53.267990 MHz pixel, and 289.771088 MHz HDMI shift.
  Bitstream SHA-256 is
  `78cd218f12feb72ccbdcb6bb141d19908c961f3438b6b559bf99b60d1c9d6940`.
- Routed SRAM candidate `F4DC1E18` proves the repaired PMMU core and K0 platform,
  not the staged K1 kernel.
- K-HW3 table-walk arbitration, K-HW4 timer/IACK changes, and K1 are integrated
  in exact build `77B3CDC8`, fully routed together, and exercised by three
  passing volatile SRAM boots plus an automatic reset-from-flash boot. Both
  physical panic paths and the bounded hardware burn-in pass.
- Exact build `66D6094F` completed a timing-clean strict route on Beast as
  useful diagnostic physical evidence. It predates the PMMU reset correction
  and cannot be a release image or be loaded onto the board.
- Exact corrected build `77B3CDC8` passes full-chip synthesis and seed-4
  critical-floorplan placement on NUC with zero SCCs, 53,073 LUT4s, 25,532
  GSR-enabled FFs, 101 DP16KDs, and 18 multipliers. Placement packs 66,513
  TRELLIS_COMB and 25,561 TRELLIS_FF cells and finishes normally with checksum
  `0x7c9a8594`. The uninterrupted no-waiver strict router1 route finishes
  normally with checksum `0x09264110`. Every constrained clock passes:
  14.179972 MHz CPU, 61.270760 MHz SDRAM, 77.760498 MHz USB, 58.227554 MHz
  pixel, and 294.290771 MHz HDMI shift. The physical-capacity gate passes at
  66,513/83,640 TRELLIS_COMB, 101/208 DP16KD, and 18/156 multipliers. Exact
  bitstream SHA-256 is
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`;
  exact build-manifest SHA-256 is
  `0593ba251da7b467e413126539d1e863ca19ef00f63843ed5f0cc6d32913b74e`.
- The exact normal `77B3CDC8` ROM now passes the complete pin-level RTL/SDRAM
  model with full 32 MiB BIST at 115.06 MB/s, PMMU enable, two isolated
  processes, 100 Hz preemption, offender-only fault death, three context
  switches, and `K1 PROTECTED ENTRY PASS`. The same image passes Musashi.
- NUC mounted the existing 244,016 MB SD volume without formatting, atomically
  validated/replaced only `/ASTRA68.ROM`, and reported exact payload CRC32
  `EB1B381F`. Normal read-only AstraHost firmware was restored and remounted the
  card. Three independent SRAM reloads of exact bitstream
  `56f768b2d78801f6cc93a7c518643f1012e30f48241e0d77be8250f97c1c2755`
  pass the complete hardware gate in 2.127, 2.145, and 2.147 seconds. Physical
  HDMI shows the exact K1 identity and complete protected-entry result; retained
  screenshot SHA-256 is
  `8b6d0d57bf7f029aa63506c348976830079ccabcdeb6a3cf38cad51d3365b051`.
  NUC then programmed the identical bitstream persistently and reset it. The
  automatic flash boot passes the same gate in 2.132 seconds; retained log
  SHA-256 is
  `deeaba2d4acdb5fbc5115085b4f751796ce11079cc68ded319c43117d17b0e97`.
- The exact direct-panic image passes on physical hardware in 1.816 seconds
  after complete POST. HDMI displays the deliberate panic, exact provenance,
  and `SYSTEM HALTED`; physical screenshot SHA-256 is
  `639785017f2691b7e4cebc493289f0e0f15d89762aed34c4994c869bce17a8de`.
  The checker transcript SHA-256 is
  `e6297e0b7adb8e2cc0352fc1c6575d6c02dbc09312059ee66ca1206ad5b8114a`.
- The exact supervisor-guard image passes on physical hardware in 1.821
  seconds after complete POST. HDMI reports vector 2, format A, SSW `0x0105`,
  exact guard address `0x02028000`, provenance, and `SYSTEM HALTED`; screenshot
  SHA-256 is
  `d7289448fb1453fee1e6be617eaad00d458d267f68183416f83ebfa1a827dce1`.
  The checker transcript SHA-256 is
  `01aa5fd5d578ad94291a82f9f771df89395274c0ac7a9a42cf702784d9abc0d0`.
- Exact source `bbb1616a1e65ef56619bffb11cb21e9ea1bc5202` removes synchronous
  address-space destruction and owner release from IPL-7 fault dispatch. Its
  64-owner ledger makes release O(frames owned) and preserves all-or-nothing
  behavior when any frame remains pinned. Eleven host suites, GCC
  `-fanalyzer`, ASan/UBSan/leak checks, 15 Rust tests, rustfmt, Clippy, all 90
  framework tests, all 30 shared Musashi/RTL executions, and both Harte smoke
  adapters pass. Exact 100-cycle Musashi completes at virtual cycle 77,501,092
  in 0.814 seconds with a 4,482-cycle masked-fault maximum. The complete
  pin-level RTL/SDRAM run completes 13 teardown cycles in 355.123 seconds with
  an 8,866-cycle maximum and no baseline drift.
- On the routed ULX3S, the same software running under unchanged production
  bitstream `77B3CDC8` completes 100 cycles in 8.219 seconds. Cycle 100 reports
  205 switches, 636 delivered ticks, syscall count `0x689`, exactly 7,987 free
  pages, and a maximum masked user-fault dispatch of 8,834 cycles (706.72 us),
  well below the 125,000-cycle gate. Retained soak transcript
  `docs/evidence/k1-77b3cdc8-bbb1616-soak-100-hw.log` has SHA-256
  `928fdd5414aacb237c5818293a464c3860ffcd0c7cf6d0a48f2fbcf200f0fb5e`.
- The normal ROM is restored at payload CRC32 `C030B951`; the read-only
  AstraHost application and production bitstream are restored unchanged. A
  fresh normal boot passes complete POST and `K1 PROTECTED ENTRY PASS` in
  1.931 seconds. Retained transcript
  `docs/evidence/k1-77b3cdc8-bbb1616-normal-hw.log` has SHA-256
  `6197aeeeb3a55ea1d8366025a6c64f9d2a424b17791bb60d3ab72d8ec6916b86`.
  That checkpoint preceded the elapsed-counter release qualification below.
- Exact source `853ae66e300232dcbdf5f69903747faa42521114` passes both
  routed-hardware release gates under the unchanged production bitstream. The
  candidate run reaches cycle 5,000 after 317.246 host seconds and
  `0x00000000EAE8411F` FPGA CPU cycles, with 10,005 switches, 31,533 delivered
  ticks, syscall count `0x15288`, 7,987 free pages, and an 8,809-cycle maximum
  masked-fault interval. The independent release run reaches cycle 29,000
  after 1,830.658 host seconds and `0x000000055263857F` FPGA CPU cycles, with
  58,005 switches, 182,861 delivered ticks, syscall count `0x7AD6B`, the same
  7,987-page baseline, and the same latency maximum. Evidence SHA-256 values
  are `db9ad4900951e3cc61ae20d8078bd714a20089bcd1880f0f77dc58d34f64dbf6`
  for the five-minute candidate and
  `71d2c3a766bc1cd25a58f6e81ca9c904517b0df74322d2d3130279a0e1ffa489`
  for the 30-minute release run.
- Normal ROM package SHA-256
  `696afc6ecf9d5df31acc76966aeea0fe190b44479c4af61a2fbf16f8866f7d05`
  is restored at payload CRC32 `BBAB0AA1`; a second one-shot boot verifies the
  exact file match. Normal read-only AstraHost and the production bitstream are
  restored unchanged. The final boot reports full Git identity
  `853ae66e300232dcbdf5f69903747faa42521114`, completes POST/BIST, and reaches
  `K1 PROTECTED ENTRY PASS` in 1.955 seconds. Retained provisioning and boot
  transcript SHA-256 values are
  `5be77adb8627fbd0ca4a6ede6601c92d362d44bf0394dce3f31fad8f0c398929`
  and `14b69338b1c429def6fa0a13067bff6e00f087dae0dc7a05a3a463e7a107f09c`.
  The hardware burn-in gate is closed; the stable-kernel mechanisms below
  remain open.

## Guarded-worker release

Exact source `42f4bb55ebd5ac47d057162322e293e4999a2661` moves process
reclamation off syscall and idle paths into a fixed stackful worker. The worker
runs S=1/M=1 on an 8 KiB MSP with a 4 KiB unmapped guard; exception and IRQ
entry use the independent guarded ISP. Pending work is a one-bit process-reap
mask, retries are one bounded bit, and all maintenance executes with interrupts
enabled. Panic output includes worker state, counters, stack use, last
supervisor IRQ, and exact maintenance failure data.

Beast passes all 12 host suites normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks; canonical m68k build verification, 15 Rust tests,
rustfmt, Clippy, all 90 framework tests, all 30 shared executions, and both
Harte smoke adapters pass. The complete Questa inventory is 141 total and 115
clean with the unchanged 3/18/5 classified buckets. Musashi normal boot reaches
K1, and its 1,000-cycle lifecycle soak ends at virtual cycle 640,260,129 with
2,001 switches and the exact 7,987-page baseline. The complete pin-level
RTL/SDRAM model passes normal K1 in 130.017 seconds and the worker soak
checkpoint in 191.959 seconds at 115.03 MB/s BIST with no baseline drift.

Exact production build `25D9CB8E` routes without a timing waiver and passes
three independent normal SRAM boots on ULX3S. Its five-minute hardware soak
reaches 5,000 cycles, 10,003 switches, 30,057 delivered ticks, 48,709 syscalls,
and `0x00000000DFEAD7D7` elapsed CPU cycles while retaining exactly 7,987 free
pages and a 9,376-cycle masked-fault maximum. After normal ROM and read-only
AstraHost restoration, a fourth SRAM boot passes. FPGA flash now contains the
same exact bitstream, and reset-from-flash reaches K1 in 2.008 seconds. NUC has
no capture device, but the retained physical-HDMI image visibly confirms the
exact Git identity, guarded worker, PMMU, preemption, fault containment, and K1
entry. Screenshot SHA-256 is
`e6e654d6ad0c9f5dead16f9116ab622d7a5ba731fc2fafc1ff7ba324c08128a4`.

## Required before K1 release

| Requirement | State |
|---|---|
| move resource destruction out of hard IRQ and IPL-7 user fault | CURRENT HW; host asserts exclusion and ULX3S reports 8,834-cycle maximum |
| supervisor stack guard in SRP | CURRENT HW; exact format-A vector-2 fault at `0x02028000` passes full RTL and physical HDMI/log |
| exact cache synchronization/alias test for loaded user code | CURRENT SIM, hardware remains |
| committed nonzero ROM/Git identity | CURRENT HW; exact `bbb1616a...` normal and soak ROMs boot on ULX3S |
| full normal/direct-panic/guard-panic RTL rerun | CURRENT from exact `77B3CDC8`; direct panic and exact `0x02028000` guard panic both preserve retained logs |
| Motorola RESET/ATC preservation and boot-flush regression | CURRENT RTL/ROUTED/HW; automatic reset-from-flash K1 boot passes |
| exact 12.5 MHz CPU / 60 MHz SDRAM complete route | CURRENT; all clocks, LUT permutation, POR, font ROM, and `kernel_platform_v1` gates pass without waiver |
| repeated ULX3S POST, SDRAM, PMMU, timer, fault, HDMI | CURRENT; three exact SRAM boots, physical HDMI, and automatic reset-from-flash boot pass |
| masked user-fault latency | CURRENT HW; 8,834 cycles against 125,000-cycle gate |
| long context/syscall/fault/allocation soak | CURRENT HW; dual-host 500,000-cycle simulation, routed five-minute/5,000-cycle candidate, and independent 30-minute/29,000-cycle release run pass at baseline 7,987 with coherent FPGA elapsed-time proof |
| panic HDMI and retained-log check on physical board | CURRENT; exact direct-panic and supervisor-guard paths pass |
| guarded-worker source exact route and ULX3S promotion | CURRENT HW; no-waiver route, repeated SRAM boots, five-minute soak, restoration, reset-from-flash, and physical HDMI pass |

## Partial or transitional K1 code

- `KernelProcess` combines one process and one thread; stable code must split
  them before waits, IPC, or multi-threaded processes.
- Four process slots and 16 handles/process are qualification limits, not the
  stable limits in `KERNEL_ARCHITECTURE.md`.
- The scheduler is round-robin only. Priority queues, bitmap selection,
  inheritance, donation, wakeup boost, and real-time budgets are not built.
- `block.c` and `dma.c` qualify ownership, generation, pin, completion, and
  revocation. Filesystem/block policy still belongs in a user service.
- The fixed K1 worker services process reap only. It is not yet a general
  scheduler-owned kernel-thread class and has no wait object, priority
  inheritance, cancellation, or per-device work queues.
- User-fault retirement is minimal and schedules the interruptible worker.
  General blocking waits and multi-threaded processes still require separate
  thread objects and the stable priority scheduler.
- The diagnostic UART mirror is emergency FTDI output only. ESP runtime
  communication is SPI.

## Not implemented

- boot allocator retirement, typed slabs/object caches, bounded general heap,
  emergency page reserve, per-subsystem allocation tags, and low-memory policy;
- separate thread objects and per-thread kernel stacks; the fixed worker has a
  canary/high-water counter and selected ISP/MSP strategy, but ordinary thread
  stacks do not yet;
- shared areas, demand-zero pages, stable-default 8 KiB page option, commit accounts, and
  cache-safe executable/file-page reclamation;
- ports, messages, handle transfer, wait-multiple, semaphores/events, deadlines,
  cancellation, priority inheritance, and priority donation;
- IRQ endpoint allocation, general deferred device workers, service restart, and
  privileged device-mapping objects;
- centralized typed MMIO accessors and complete bus-timeout classification;
- fixed allocation-free trace ring and interactive SPI/FTDI monitor commands;
- graphics-service command ring/mapping revocation and dedicated graphics RAM
  arena;
- full deterministic allocation failure injection.

## Known invariants and tests

| Invariant | Test state |
|---|---|
| user cannot map kernel/page-table frames | host VM tests |
| null and W+X mappings rejected | host VM tests |
| stale handles cannot reach reused slot | host handle tests |
| killed owner releases mappings/handles/frames | host process test, target K1 |
| pinned DMA delays but does not lose teardown | host process/DMA/block tests |
| user-copy fault returns error, unrelated kernel fault panics | host and full target |
| nested timer on supervisor exception resumes handler | host dispatch and focused RTL |
| M=1 IRQ returns through format-1 ISP then format-0 MSP frame | Motorola-directed Questa plus full pin-level normal/soak |
| worker cannot lose signal at service or block boundary | host worker state machine plus target lifecycle soak |
| worker stack guard is invalid; canary/high-water remain readable | host VM/worker tests plus Musashi/full RTL boot |
| fault after repeated traps stacks on ISP | focused Questa wait/zero-wait |
| hard timer path performs no maintenance/destruction | host dispatch test |
| user-fault path performs no maintenance/owner release | host dispatch/process tests; bounded Musashi, full RTL, and ULX3S reports |
| owner release visits only that owner's frames | host memory test, visit counter |
| pinned owner release is atomic and owner slots are bounded/reusable | host memory tests |
| kernel stack guard descriptor is invalid and adjacent pages remain mapped | host VM test |
| kernel stack guard access reaches retained panic path | full RTL and physical hardware at exact address `0x02028000` |
| duplicate user cached alias is rejected and reusable after unmap | host VM test |
| cross-CRP same-address code/data cannot expose stale bytes | Musashi and full RTL K1 |
| malformed syscall corpus never panics | MISSING |
| every allocation site unwinds exactly | MISSING |

## Next actions

1. route exact guarded-worker source at 12.5 MHz CPU and 60 MHz SDRAM, then
   repeat POST, PMMU, worker lifecycle, reset, and HDMI gates on NUC;
2. split process and thread objects before adding waits, IPC, and
   multi-threaded scheduling;
3. implement and benchmark 8 KiB against the retained 4 KiB oracle before
   freezing the stable VM ABI.
