# Astra 68 kernel test and fault-injection plan

Status: normative qualification plan, revision 0.1 (2026-07-23)

No subsystem is complete because its happy path boots. Every state transition,
capacity limit, cancellation race, and recovery path has a deterministic test.
Musashi, RTL, and hardware run the same target binaries and assertions; only
soak duration may scale by backend speed.

## Test layers

| Layer | Purpose | Required on every relevant change |
|---|---|---|
| host unit | allocators, tables, queues, state machines, overflow | yes |
| host sanitizer/analyzer | UB, bounds, leaks, lifetime paths | yes |
| Musashi | exact kernel image, PMMU/syscalls/faults, long deterministic soak | yes |
| focused RTL | one architectural or bus contract with strict oracle | yes for RTL/arch changes |
| full pin-level RTL | exact ROM, SDRAM, IRQ, PMMU, graphics coexistence | yes before route |
| routed ULX3S | timing-qualified physical behavior | release gate |

The shared conformance framework owns CPU/PMMU cases. An adapter may translate
transport, but expected initial/final state and pass criteria remain identical.

## Current K1 gate

The candidate must retain all of these before routing:

- 11 kernel host suites, GCC `-fanalyzer`, ASan/UBSan, and leak detection;
- 15 AstraVM Rust tests, rustfmt, and Clippy `-D warnings`;
- 90 shared framework tests and all 30 executions of the 15-case Musashi/RTL
  matrix;
- both maintained Harte MC68030 smoke adapters;
- strict Questa inventory: 140 total, 114 clean, with no increase or
  reclassification of the recorded 3 compile, 18 simulation, and 5 unscored
  upstream buckets;
- focused PMMU user fault after traps in wait-state and zero-wait modes;
- Motorola CACR mixed-command decode, including simultaneous CI/CD;
- full SoC normal K1, deliberate direct-panic, and exact stack-guard-panic images;
- PMMU boot with explicit pre-enable ATC flush and separate I/D cache clears;
- MC68030 processor reset with populated ATC and roots: clear only TC/TT enable
  bits, observe the retained stale translation, then prove boot `PFLUSHA`
  forces a walk to the updated descriptor before translation is enabled;
- fault dispatch must perform no synchronous process maintenance or owner-frame
  release, and the next qualifying soak checkpoint must be preceded by a
  positive `user_fault_irqoff_max` report no greater than 125,000 CPU cycles;
- owner-ledger exhaustion, reuse, pinned-release atomicity, corrupt-link
  rejection, and release visits proportional only to the owner's frame count;
- all directed and integrated graphics coexistence tests.

Known upstream failures are tracked evidence, not waivers. A new mismatch is a
regression until classified against Motorola behavior and fixed or given a new
Motorola-corrected oracle.

## Deterministic failure injection

Every fallible kernel allocation has a stable numeric site ID. Debug builds can
fail the Nth call globally or the Nth call at one site. Each operation runs with
failure injected at every allocation point and must leave pool, reference,
pin, mapping, queue-byte, and commit counters equal to its captured baseline.

Required injection controls:

| Class | Injection |
|---|---|
| memory | frame/slab/heap failure, emergency-reserve exhaustion |
| user copy | fault on every byte position and page boundary |
| VM | root/leaf allocation failure, stale ATC, map/unmap/protect repetition |
| handles | table full, stale generation, reuse wrap diagnostic |
| IPC | queue full, byte budget full, receiver slot failure, peer death during transfer |
| waits | signal/timeout/cancel/peer-death race permutations |
| timer | cancel at expiry, late programming, counter rollover |
| IRQ | spurious, storm, reassert during ack, record overflow |
| device | completion lost, stale generation, physical bus timeout, reset failure |
| process | death during IPC, DMA, mapping, wait, and service RPC |
| cache | executable load synchronization and cache-policy alias rejection |
| MMIO | misalignment, width, CPU ordering, posted-write fence, and dead target |

Randomized tests use a printed fixed seed and operation log so every failure is
replayable. Random malformed syscalls and messages must never panic the kernel.

## State-machine suites

### Allocator and VM

- classify every BootInfo range, overlap, gap, overflow, and invalid ordering;
- allocate/free every alignment and end-of-RAM boundary;
- retain/pin/unpin/release with overflow, underflow, wrong owner, and teardown;
- create/destroy 4 KiB and 8 KiB roots under every table allocation failure;
- map/unmap/protect across leaf boundaries and reject mixed cache aliases;
- reject a second cached user alias of one frame and permit remap only after
  the first descriptor is removed and cache/ATC maintenance completes;
- verify null, user-stack, kernel-stack, page-table, MMIO, and ROM protections;
- load code, synchronize caches, execute, unmap, reuse, and prove no stale data.

### Handles, IPC, and waits

- stale use, type mismatch, each right, duplicate reduction, transfer atomicity,
  generation reuse, table exhaustion, and close-all callbacks;
- empty/full port, byte limit, blocking/nonblocking/deadline send/receive;
- peer close with blocked senders/receivers and committed/uncommitted handles;
- wait-multiple simultaneous readiness, cancellation, timeout, and object death;
- priority inheritance and RPC donation, including timeout rollback.

### Scheduler and exceptions

- patterned values in every saved register, USP, PC, SR, CRP, and stack;
- same-CRP and cross-CRP switches, every priority, quantum expiry, and wakeup;
- nested interrupt during syscall/fault entry and supervisor-frame resume;
- Motorola formats 0, 1, 2, 9, A, and B with exact byte fixtures;
- copyin/copyout user fault versus physical bus failure;
- final-process exit/fault into supervisor idle and later interrupt wakeup;
- runaway real-time demotion and input/system-service survival.

### Devices

- queue and byte limits, wrap-safe address validation, pin quota, and cache mode;
- service death before submit, in flight, on completion, and during reset;
- stale completion after slot/device generation changes;
- accelerator timeout and reset while unrelated CPU, video, and storage work
  continues;
- graphics clipping, vblank/fence completion, active-buffer protection, and
  command-ring overflow.
- distinguish CPU-side write synchronization from downstream posted-write or
  accelerator completion using the documented readback/fence for each device.

## Soak gates

The same deterministic target workload repeatedly allocates processes and
areas, issues syscalls, switches contexts, faults one process, transfers and
closes handles, and returns every counter to baseline.

| Backend | Minimum |
|---|---:|
| host state machines | 1,000,000 operations/seed, 100 seeds |
| full RTL nightly | 1,000 context switches and 100 complete teardown cycles |
| Musashi | 1,000,000 context switches and 100,000 teardown cycles |
| routed ULX3S candidate | 1,000 teardown cycles and at least 5 continuous minutes |
| routed ULX3S release burn-in | 30 continuous minutes, at least 5,000 teardown cycles, and cycle-counter proof of elapsed time |
| routed ULX3S reboot | 10 warm resets and 3 cold power cycles |

Long exhaustive repetition belongs on host and Musashi, where it can run in
parallel without monopolizing the only development board. Physical testing
concentrates on failure modes simulation cannot establish: the routed image,
real SDRAM, clocks, CDC paths, reset behavior, HDMI, SPI, and thermal stability.
An optional longer burn-in may run when the board is otherwise idle, but it is
not a prerequisite for unrelated kernel development.

Exact source `bbb1616a1e65ef56619bffb11cb21e9ea1bc5202` passes the bounded
candidate checkpoint on the routed ULX3S: 100 complete teardown/relaunch cycles
in 8.219 seconds, 205 switches, 636 delivered ticks, a nonzero syscall count,
and the unchanged 7,987-page baseline. The maximum masked user-fault dispatch
is 8,834 cycles. This is the fast per-change hardware gate; it does not replace
the 1,000-cycle/five-minute candidate run or 30-minute release burn-in above.

Exact source `853ae66e300232dcbdf5f69903747faa42521114` closes both routed
hardware gates with the unchanged production bitstream. The target reads the
cycle-counter low word first to latch a coherent 64-bit snapshot, reads the high
word second, and reports wrap-safe elapsed cycles on every complete checkpoint.
The checker requires the announced free-page baseline, nonzero switches,
syscalls and delivered ticks, bounded masked-fault latency, monotonic counters,
and a newline-terminated checkpoint at or above every requested threshold.

- Candidate: cycle 5,000 after 317.246 host seconds and
  `0x00000000EAE8411F` FPGA CPU cycles; 10,005 switches, 31,533 delivered
  ticks, syscall count `0x15288`, 7,987 free pages, and 8,809-cycle maximum
  masked-fault latency. This exceeds the exact five-minute threshold
  `0x00000000DF847580`.
- Independent release reset: cycle 29,000 after 1,830.658 host seconds and
  `0x000000055263857F` FPGA CPU cycles; 58,005 switches, 182,861 delivered
  ticks, syscall count `0x7AD6B`, 7,987 free pages, and the same 8,809-cycle
  latency maximum. This exceeds the exact 30-minute threshold
  `0x000000053D1AC100` and the 5,000-cycle minimum.

Retained evidence is
`docs/evidence/k1-77b3cdc8-853ae66-candidate-5m-hw.log`, SHA-256
`db9ad4900951e3cc61ae20d8078bd714a20089bcd1880f0f77dc58d34f64dbf6`,
and `docs/evidence/k1-77b3cdc8-853ae66-release-30m-hw.log`, SHA-256
`71d2c3a766bc1cd25a58f6e81ca9c904517b0df74322d2d3130279a0e1ffa489`.

The scheduler's delivered-interrupt count is not an elapsed-time oracle. Vesta
uses one pending expiration bit, so periods coalesce while interrupts are
masked. Hardware soak timing must use the 64-bit CPU cycle counter or an
independent host clock, and must report both elapsed periods and delivered
interrupts. A large discrepancy is a latency failure to investigate, not a
reason to extend the soak.

Pass requires zero panic, hang, unexpected fault, stale completion, monotonic
memory growth, reference/pin drift, queue growth, or output mismatch. First and
last snapshots include free pages, every pool, handles, mappings, pins, queued
bytes, faults, IRQ counts, and context switches.

## Timing and responsiveness

Hardware tests record maximum and percentile values for IRQ top-half duration,
interrupt-disabled time, scheduler lock, wake-to-run latency, same/cross-CRP
switch, syscall, copy, map/unmap, ATC miss, and device reset. The limits in
`LOCKING_AND_PREEMPTION.md` are release failures, not informational warnings.

## Panic and retained diagnostics

Injection must panic only for an internal invariant or explicit panic test.
The test verifies HDMI text, build version/date/Git identity, exception fields,
scratch status, and retained early-log panic flag after reset. A panic path
performs no allocation and never depends on a user service.

## Release evidence

Each retained run records source commit, dirty state, tool/host/version, ROM and
bitstream hashes, build ID, seed/configuration, test counts, exact failures,
resource use, all constrained clocks, and hardware observation. Reduced-feature
or placement-only runs are diagnostics, never release evidence.
