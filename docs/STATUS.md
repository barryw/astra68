# Astra 68 kernel status

Status date: 2026-07-24

This is the kernel-specific truth table. `CURRENT_STATE.md` remains the whole
machine continuation map. A row marked CURRENT has evidence; PLANNED or MISSING
must not be presented as working software.

## Current source identity

- Branch: `main`.
- K4 handle-synchronization source commit:
  `662aa04ef807d6c74ea1a8d0c3a95b8eb78931e7`. The synchronization
  implementation landed in `4a878c9095213d9009e3ad6eeca85ebac3d7c936`; the
  later source delta adds exact K4 RTL and hardware acceptance only. The
  immutable qualification source archive has
  SHA-256
  `ee06971a5239d890ea9e157ae46638bdefa94d3eb86c13656e1aaeedf00d6fe3`
  and is extracted as `/tmp/astra68-k4-662aa04` on Beast and NUC.
- K4 kernel: 44,740 bytes, SHA-256
  `11c2ed31ca5caf07dcfbd87cf354f6ce7be3eb1873cef412b65a6821940fb91c`.
- K4 normal boot payload: 56,152 bytes, CRC32 `2F9B149C`, SHA-256
  `15f713f45e5e8b1eec1bf9820759e915186b70339d3704c0e0771d35df47e588`.
  The 56,184-byte packaged ROM SHA-256 is
  `14f4f980ebeb3fed099ac44d3035e6a1d6f1b2aa354b87bd208c468eb1b66c28`.
  K4 is qualified through exact pin-level RTL and two ULX3S boots on unchanged
  routed FPGA build `25D9CB8E`.
- K3 one-shot/deadline baseline commit:
  `3787d820e1140f49ba31623ccc578bb274a631cc`. Its retained target artifacts
  report the exact development identity
  `8929c063cdd24c8f4f526be330549e2eb5038fc8-dirty` and remain the qualified K4
  rollback checkpoint.
- K3 kernel: 41,020 bytes, SHA-256
  `6ab38364d2ef5e67b6f5e8c7fb691cbf45291624562d7a0203f812c2e648e61d`.
- K3 normal boot payload: 52,444 bytes, CRC32 `BAEF4D0B`, SHA-256
  `33009b3eb09ae51d3ebcdbeac57ec7aff9d3aadee6ee34ab4ea550bc1e76e2c7`.
  The 52,476-byte packaged ROM SHA-256 is
  `b73964d87904994a570c3b5e2b931602f8eb7878f0b531c0ac7e775050919ab1`.
  This software-only image reuses exact routed FPGA build `25D9CB8E`.
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
| typed generation handle table | CURRENT | host tests; current 16 entries/process |
| process creation and owner teardown | CURRENT HW | host, Musashi, full RTL, and exact 100-cycle ULX3S fault/reap path |
| separate generation-safe process/thread objects | CURRENT HW | host, Musashi, full RTL, and two exact K2 ULX3S boots; 4 process and 16 thread slots |
| guarded per-thread supervisor stacks | CURRENT HW | 16 8 KiB stacks with 4 KiB guards; host mapping/canary tests, Musashi, full RTL, and two ULX3S boots report 388-byte maximum use |
| multi-thread process teardown | CURRENT HW | all siblings leave ready/wait queues atomically; deferred owner reap passes host, Musashi, full RTL, and ULX3S |
| deferred pinned-DMA reap | CURRENT SIM | guarded worker state machine on host; Musashi and full RTL lifecycle soaks |
| two isolated user processes | CURRENT HW | same ROM on Musashi, full RTL, and three exact SRAM boots |
| 5 ms one-shot fixed-priority scheduling | CURRENT HW | exact 62,500-cycle quantum, 32 queues and ready bitmap; highest priority first, FIFO round-robin among equals; K3 target boots pass |
| same-address-space thread switch | CURRENT HW | host, Musashi, full RTL, and ULX3S count this path separately without a CRP/ATC/cache switch |
| K3 atomic block/wake/deadline substrate | CURRENT HW | sequence-checked wait queues, 16-entry deadline heap, priority/FIFO wake, timeout, close wake-all, and immediate higher-priority handoff pass host, Musashi, full RTL, and ULX3S |
| handle-backed events and semaphores | CURRENT HW | generation-safe handles, explicit rights, absolute-nanosecond deadlines, cancellation, close, owner death, quotas, and exact-once arbitration pass host, exact Musashi, pin-level RTL, and two ULX3S boots |
| trap ABI query/progress/yield/exit/close/clock/sync | CURRENT HW | retained K1 calls and K4 clock, create, wait, signal, reset, and cancel calls pass host, Musashi, pin-level RTL, and ULX3S |
| offender-only user fault death | CURRENT HW | format-B fault reaps only the offender on Musashi, full RTL, and three exact SRAM boots |
| last-process supervisor idle transition | CURRENT HOST | process/dispatch tests; target assembly builds |
| panic to console and retained early log | CURRENT HW | exact direct and supervisor-guard panic paths pass full RTL plus physical HDMI/log qualification |
| kernel host analyzer/sanitizer gates | CURRENT | 17 suites, analyzer, ASan/UBSan/leak checks |
| kernel cycle-budget gate | CURRENT HW | nine measured syscall/timer/fault/scheduler/wait/deadline paths enforce fixed limits in Musashi, full RTL, and ULX3S; zero overruns |
| end-to-end Musashi performance gate | CURRENT | exact 1,000-cycle K4 workload is 622,507,501 virtual cycles against a 675,000,000-cycle cap |
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
| K3 one-shot/deadline hardware boot | CURRENT HW | two exact `25D9CB8E` reloads pass ROM `BAEF4D0B`, full 32 MiB POST/BIST, 5 ms quantum, timeout handoff, all K1/K2/K3 markers, and zero overruns |
| K4 handle-synchronization hardware boot | CURRENT HW | two exact `25D9CB8E` reloads pass ROM `2F9B149C`, full 32 MiB POST/BIST, all event/semaphore lifecycle counts, priority handoffs, K1-K4 markers, and zero overruns |

## Hardware status

- The ULX3S attached to NUC now runs exact persistent guarded-worker release
  `25D9CB8E`. Prior `77B3CDC8` K1 and `6C0D0CA3` K0 images remain qualified
  rollback artifacts, not the board's current flash contents.
- The same `25D9CB8E` bitstream now boots K4 ROM CRC32 `2F9B149C` from SD after
  two independent volatile reloads. FPGA flash was not rewritten. Normal
  read-only AstraHost firmware is restored, and `/ASTRA68.ROM` is the only FAT
  file changed by the provisioning run.
- NUC currently enumerates no HDMI capture device. Both K4 hardware transcripts
  prove that the console generated every K4 line and marker; the unchanged
  routed HDMI pipeline retains its exact K1 physical screenshot qualification.
  A new physical K4 screenshot is a visual evidence follow-up, not an RTL,
  scheduler, SDRAM, or ROM-identity failure.
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

## K2 blocking/thread development checkpoint

The scheduler dispatches independent `KernelThread` objects rather than a
combined process/context record. Sixteen fixed 156-byte records carry
generation IDs, process-private handles, CPU contexts, guarded user and 8 KiB
supervisor stacks, base/effective priority, accounting, and intrusive ready and
wait links. Processes own the CRP/address space, resource account, handle table,
default priority 16, user ceiling 23, and aggregate death. Current handle-table
capacity makes the development per-process cap 15 threads; the stable target
remains 16 after the handle pool grows.

Thirty-two FIFO queues plus one bitmap select the highest effective priority in
bounded constant steps and rotate equal priorities round-robin. Same-process
switches retain CRP and avoid the VM switch/cache/ATC path. A 12-byte wait queue
uses a monotonic nonzero sequence to make condition-check plus block atomic;
wake-one selects the highest-priority waiter and preserves FIFO order among
equals, while wake-all is bounded by the 16-thread pool. The first 16-byte event
implements signaled, consumed, and closed states on that queue. Process death
withdraws every sibling from ready and wait queues before deferred destruction
and generation-safe record reuse.

All 16 host suites pass normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks. Canonical MC68030 build verification, 15 Rust tests,
rustfmt, and Clippy `-D warnings` pass. Shared out-of-line byte copy/clear
primitives replaced repeated force-inlined loops; exhaustive alignment/length
tests cover them and target disassembly uses aligned longword operations. The
exact 1,000-cycle Musashi workload fell from 895,512,627 to 645,759,523 virtual
cycles. Replacing duplicate DMA/block generation rollover with the existing
shared helpers removed 12 image bytes and another 436 cycles, leaving
645,759,087 cycles. A 675,000,000-cycle automated ceiling now makes that
end-to-end result a regression gate. The normal kernel is 36,960 bytes with
SHA-256
`1ee4232e1cc7c637f4c0ea06787191193fb23c64a19b603fc51d89539b425a29`.

The coherent full pin-level Verilator/SDRAM run uses the same ROM payload and
exact dirty source identity. Full 32 MiB BIST passes at 115.04 MB/s. It executes
seven context switches, two same-CRP switches, two blocks, one wake, one
higher-priority handoff, and reports a 388/8192-byte maximum supervisor-stack
use. Maximum measured cycles are 20,806 syscall dispatch, 14,344 timer dispatch,
21,896 user fault, 1,449 scheduler pick, 1,567 same-CRP switch, 2,540 cross-CRP
switch, 1,265 block, and 2,437 wake. Every fixed budget passes with zero
overruns, followed by K2 blocking/thread and retained K1 markers.

The HDMI-enabled boot payload is 48,384 bytes with CRC32 `E28408B4`; the
packaged ROM is 48,416 bytes. Two independent volatile reloads of exact routed
build `25D9CB8E` on the NUC-attached ULX3S complete the same gate in 2.333 and
2.338 seconds. Both report the same 388-byte stack high-water mark and zero
cycle budget overruns. Run-to-run maxima vary only slightly: syscall
29,705/29,685, timer 14,310/14,335, user fault 19,632/19,635, scheduler pick
1,444/1,444, same-CRP 1,575/1,569, cross-CRP 2,546/2,527, block 1,264/1,264,
and wake 2,436/2,428. FPGA flash was not rewritten; normal read-only AstraHost
firmware and the exact K2 ROM were restored while unrelated SD contents were
preserved.

Retained final evidence is
`evidence/k2-be27074-refactor-perf-rtl.log` (SHA-256
`7eb2ffa99c6d9d270acbb51de05ebe7807a16afc5f3a7696b0624c68b80d9047`),
`evidence/k2-25d9cb8e-be27074-refactor-perf-provision.log`
(`36383d6e3d1e421ca77718d05473136f5fda66bd284e79d610904ef36859f0ee`),
`evidence/k2-25d9cb8e-be27074-refactor-perf-hw-1.log`
(`09a76d063c7743e386f66100d53eeda75707dc5087b5364e3436ad1d03441cbf`),
and `evidence/k2-25d9cb8e-be27074-refactor-perf-hw-2.log`
(`2e75d86ee06d0efcc219e0cbd258bfb693f902842823dfdbd99798981fdd74bf`).
A mixed-source scratch simulation and an accidental `77B3CDC8` rollback load
were rejected before acceptance; neither is a kernel failure or evidence for
this checkpoint.

## K3 one-shot/deadline checkpoint

K3 replaces the periodic 10 ms scheduling tick with an exact 5 ms one-shot
quantum (62,500 cycles at 12.5 MHz). The timer is rearmed to the earlier of the
active quantum and earliest blocked-thread deadline. Context activation starts
a quantum; ordinary syscalls do not renew it. Higher-priority readiness from a
signal or timeout preempts immediately, while an all-blocked system retains the
deadline and idles through the guarded supervisor worker.

The deadline owner is one fixed 16-entry binary min-heap, one entry per global
thread slot. It stores absolute 64-bit cycle deadlines, orders equal deadlines
by slot, allocates nothing, and removes timeout/signal/close/process-death
waiters exactly once. The current timed wait remains an internal qualification
path; public handles, absolute monotonic nanoseconds, explicit cancellation,
and peer-death ABI remain the next object-lifecycle work.

All 17 kernel host suites pass normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks. Canonical m68k verification, all Rust format/Clippy/test
gates, all 90 framework tests, all 30 shared executions, both Harte smoke
adapters, and the directed Vesta timer/IACK race test pass. Exact normal
Musashi reaches K3 at 17,250,229 virtual cycles. The exact 1,000-cycle workload
finishes at 596,507,297 cycles, 49,251,790 cycles below K2 and 78,492,703 below
the enforced 675,000,000 ceiling, with 2,036 switches, 3,103 delivered timer
interrupts, syscall count `0x1F85`, 7,986 free pages, and zero overruns.

The complete pin-level RTL/SDRAM model uses an intentional 64 KiB simulated
BIST and reaches all K1/K2/K3 markers in 222.222 seconds. It measures deadline
expiry at 6,163/20,000 cycles with zero overruns. NUC mounted the existing
244,016 MB card without formatting, atomically replaced only `/ASTRA68.ROM`,
and independently verified the installed file. Provisioning evidence SHA-256
values are
`e541070f0a086e3a2c29177c52611709d27040f0302719bc80df8fc28004f0a9`
and
`58c58ac2f1c55990b17690b19fa921fa9c2c710061aea065c56836a260adca4e`.

Two independent volatile loads of exact production bitstream `25D9CB8E` then
pass ROM CRC32 `BAEF4D0B`, complete POST and real 32 MiB BIST, PMMU/user-copy
isolation, 20 context switches, one timeout and higher-priority handoff, all
K1/K2/K3 markers, and zero overruns. Both measure deadline expiry at
6,177/20,000 cycles. Hardware transcript SHA-256 values are
`f05c5f0a6b88ab38fb3557d6f412dfbd708011b5078112eaa905b515a0856709`
and
`f5f46ccd4230aca44a360a402dc57747e42aef2a8f56461f55960a3bd8ceaa55`.
Read-only AstraHost is restored. This is a software-only checkpoint: FPGA
flash, routed resources, and every constrained-clock result remain unchanged.

## K4 handle-synchronization checkpoint

K4 publishes auto-reset/manual-reset events and counting semaphores through
per-process generation-safe handles. Handles require explicit wait or signal
rights. Waits accept signed absolute monotonic nanosecond deadlines, with zero
as poll and `INT64_MAX` as no deadline. Signal, timeout, explicit cancellation,
last-handle close, and owner death remove and complete a waiter exactly once.
Wake-one is priority ordered and FIFO among equals; waking a higher-priority
thread requests an immediate handoff.

The implementation allocates 32 fixed 36-byte objects, limits each owner to
eight objects, and inherits the global 16-waiter ceiling from the thread pool.
It reuses each thread's existing wait/deadline linkage and allocates nothing in
the signal, wait, timeout, cancellation, or teardown paths. The syscall ABI now
provides monotonic time, event/semaphore creation, wait-one, signal, event
reset, and cancellation. Public NDK rights use the same canonical bit values as
the kernel ABI.

All 17 host suites pass normally, under GCC `-fanalyzer`, and under
ASan/UBSan/leak checks. NDK `check`, sanitizer, analyzer, m68k library/example,
and canonical m68k kernel/ROM verification pass. Exact committed normal Musashi
reaches all K1-K4 markers in 19,000,216 cycles, with 19 context switches, six
blocks, two wakes, three priority handoffs, one expiry, and
cancel/close/death counts of 1/1/1. The exact 1,000-cycle workload completes in
622,507,501 cycles under the 675,000,000 cap, retains exactly 7,986 free pages,
reports 2,055 switches, 3,204 timer ticks, syscall count `0x2377`, and has zero
performance overruns. Maximum masked user-fault time is 34,580 cycles.

The exact pin-level RTL/SDRAM model passes its intentional 64 KiB BIST, all
lifecycle counts, every K1-K4 marker, and all performance gates in 266.959
seconds. It reports 27 context switches, six same-CRP switches, a
6,164/20,000-cycle deadline maximum, and zero overruns. Retained transcript
SHA-256 is
`fa89ee4c9188866a20aed4ced11d90d7391a8455f0ef4fc1fd6614619ed661da`.

NUC mounted the existing 244,016 MB card without formatting, replaced only
`/ASTRA68.ROM`, and independently reported that the installed file already
matched. Provisioning transcript SHA-256 values are
`e3d0d21efbfae8d4a84efe5318ee495329d8d4fe8655da35799c16e8524c0be0`
and
`3d7aae0c20165175fdffdf51655c5ebd4fafae82fd9f2fa6371bb8952bb33e78`.
Known read-only AstraHost was then restored.

Two independent volatile loads of exact production bitstream `25D9CB8E` pass
ROM CRC32 `2F9B149C`, complete POST and physical 32 MiB BIST, PMMU/user-copy
isolation, 27 context switches, six blocks, two wakes, three priority
handoffs, one deadline expiry, cancel/close/death counts of 1/1/1, all K1-K4
markers, and zero overruns. Both report a 6,164/20,000-cycle deadline maximum.
Hardware transcript SHA-256 values are
`4dd8583781bca253229240e25f4169d70aaa1a5a224b22be24d4aef23d2c3135`
and
`b614522dd02fd9f110b56196297dd7afb221165c60e5383424abd3a7e1139de6`.
This is a software-only promotion: FPGA flash, routed resources, and every
constrained-clock result remain unchanged.

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

## Partial or transitional current code

- Four process slots, 16 global thread slots, 16 handles/process, and the
  resulting 15-thread/process cap are qualification limits, not the stable
  limits in `KERNEL_ARCHITECTURE.md`.
- The fixed-priority queue/bitmap scheduler, 5 ms one-shot quantum, and bounded
  absolute-cycle deadline heap are current. Runtime high-priority signal and
  timeout handoff are proven through the public K4 handle path on host and
  Musashi. Address-space affinity among equal priorities, inheritance,
  donation, wakeup boost, and real-time budgets are not built.
- Thread creation is an internal boot API. No stable create/join/exit-thread
  syscall exists, and provisional `EXIT` still terminates the whole process.
- Ordinary user threads have guarded user and supervisor stacks. K4 events and
  semaphores have a documented revision-0.1 handle/syscall contract, but thread
  death, process death, timers, ports, and wait-multiple are not yet exposed as
  general waitable handles.
- `block.c` and `dma.c` qualify ownership, generation, pin, completion, and
  revocation. Filesystem/block policy still belongs in a user service.
- The fixed K1 worker services process reap only. It is not yet a general
  scheduler-owned kernel-thread class and has no general wait object, priority
  inheritance, or per-device work queues.
- User-fault retirement is minimal and schedules the interruptible worker.
  K4 synchronization cancellation and owner-death arbitration are current;
  wait-multiple and service/port peer-death semantics remain.
- The diagnostic UART mirror is emergency FTDI output only. ESP runtime
  communication is SPI.

## Not implemented

- boot allocator retirement, typed slabs/object caches, bounded general heap,
  emergency page reserve, per-subsystem allocation tags, and low-memory policy;
- shared areas, demand-zero pages, stable-default 8 KiB page option, commit accounts, and
  cache-safe executable/file-page reclamation;
- ports, messages, handle transfer, wait-multiple, priority inheritance, and
  priority donation;
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
| stale thread IDs cannot reach reused slot, including slot 15 | host thread tests |
| higher priority wins and equal priorities remain FIFO round-robin | host thread/process tests plus K3 target boot |
| same-process switch preserves CRP and bypasses VM switch accounting | host process test, Musashi, full RTL, and ULX3S K3 boots |
| stale wait sequence cannot block after a condition change | host thread/sync tests |
| wake-one is priority ordered and FIFO among equals | host thread test plus target priority-handoff path |
| event close wakes each waiter exactly once | host sync/process tests plus exact K4 Musashi |
| process death withdraws every sibling from ready/wait queues before deferred record reuse | host process test, Musashi soak, full RTL, and ULX3S K3 boots |
| each thread supervisor stack has an unmapped guard, valid canary, and bounded high-water | host VM/thread tests, Musashi, full RTL, and ULX3S K3 boots |
| every measured K3 hot path stays within its fixed cycle budget | host profiler tests, Musashi, full RTL, and two ULX3S boots |
| 1,000-cycle Musashi workload remains at or below 675,000,000 virtual cycles | automated emulator performance gate |
| ordinary syscalls do not renew the running thread's quantum | host process test, Musashi, full RTL, and two K3 ULX3S boots |
| timer always targets the earlier active quantum or wait deadline | host platform/process tests plus K3 target timeout handoff |
| equal deadlines are deterministic and deadline capacity equals thread capacity | host thread tests |
| timeout, signal, cancellation, close, and process death remove one deadline/waiter exactly once | host thread/sync/process tests plus exact K4 Musashi |
| higher-priority timeout wake preempts immediately | host process test, Musashi, full RTL, and two K3 ULX3S boots |
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

1. run exact K4 through the complete pin-level RTL/SDRAM model, then provision
   and repeat the gate on the NUC-attached ULX3S without changing the FPGA
   bitstream;
2. add stable create/join/exit-thread operations and account every stack/object
   against process quotas;
3. add one bounded wait-multiple operation over events, process/thread death,
   and future ports/timers without helper threads;
4. implement and benchmark 8 KiB against the retained 4 KiB oracle before
   freezing the stable VM ABI.
