# Astra 68 kernel test and fault-injection plan

Status: normative qualification plan, revision 0.1 (2026-07-22)

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
| routed ULX3S | 24 continuous hours and at least 8,640,000 timer ticks |
| routed ULX3S reboot | 100 warm resets and 25 cold power cycles |

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
