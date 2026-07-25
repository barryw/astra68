# Axiom test and fault-injection plan

Status: normative qualification plan, revision 0.2 (2026-07-25)

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

## Current kernel gate

The candidate must retain all of these before routing:

- 17 kernel host suites, GCC `-fanalyzer`, ASan/UBSan, and leak detection;
- 15 AstraVM Rust tests, rustfmt, and Clippy `-D warnings`;
- 90 shared framework tests and all 30 executions of the 15-case Musashi/RTL
  matrix;
- both maintained Harte MC68030 smoke adapters;
- strict Questa inventory: 141 total, 115 clean, with no increase or
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
- the guarded worker must cover signal coalescing, signal-during-service,
  deferred pinned-DMA retry, timer wake, atomic idle, user return, canary, and
  high-water accounting without allocation or unbounded queues;
- the thread pool must cover every priority 0-31, FIFO order among equals,
  highest-priority selection, generation reuse, invalid-input rollback, global
  and per-process exhaustion, and exact frame reclamation;
- guarded per-thread supervisor stacks must cover descriptor protection,
  canary corruption, bounded high-water scanning, and entry accounting;
- wait queues must cover stale-sequence rejection, duplicate block rejection,
  priority/FIFO wake-one, bounded wake-all, close, and process retirement with
  no lost wakeup;
- the one-shot scheduler must cover an exact 62,500-cycle quantum, earliest-of
  quantum/deadline programming, zero-delay clamping, long-delay clamping,
  ordinary syscalls that do not renew a quantum, supervisor idle wakeup, and
  immediate higher-priority timeout handoff;
- the 16-entry deadline heap must cover equal-deadline deterministic ordering,
  full capacity, already-expired rejection, signal/timeout ordering, close,
  and process-death removal with one result and no stale heap entry;
- the 32-entry synchronization pool must cover full-system and per-process
  quota exhaustion, handle-table publication rollback, generation-safe reuse,
  invalid type and rights, 16-waiter capacity, and a return to its exact
  baseline after final close and process death;
- auto-reset events must cover retained signal, priority/FIFO wake-one, and no
  duplicate wake; manual-reset events must cover wake-all, retained signaled
  state, reset, and post-reset blocking;
- semaphores must cover immediate decrement, direct waiter handoff, FIFO among
  equal priorities, count remainder, maximum overflow rejection with no partial
  wake, and close with queued waiters;
- signal/timeout/cancel/final-close/creator-death permutations must prove one
  terminal result, one queue withdrawal, one deadline removal, and no object or
  reference leak;
- public waits must cover absolute nanosecond poll, finite, infinite, invalid
  negative, rounding, and high-word deadlines plus monotonic-clock rollover;
- production target scheduling must exercise handle creation, rights-checked
  wait/signal, event signal handoff, semaphore handoff, timeout, cancellation,
  close wakeup, and stale-handle rejection before the K4 marker;
- K5 thread creation must cover every invalid entry/priority/right input,
  global thread exhaustion, process quota exhaustion, handle-table exhaustion,
  physical-frame failure, page-table allocation failure, and publication
  rollback; every failure must preserve exact handle, mapping, frame, ready,
  stack-slot, and quota baselines;
- a deterministic publication race must inject a supervisor timer at the final
  interrupt-enable to commit-mask boundary, expire and enqueue an existing
  waiter, publish the prepared thread, and prove both ready links, counts, and
  queue invariants remain exact;
- thread exit must cover a blocked waiter, immediate repeated wait, 32-bit exit
  status, timeout, cancellation, final-handle close, stale-handle reuse,
  close-before-exit, last-thread process promotion, and process death while
  sibling threads are ready and blocked;
- K5 maintenance must prove that an exited thread's user stack is reclaimed on
  the guarded worker rather than its own supervisor stack, that a zombie keeps
  only its documented record/supervisor charge, and that join/close/create
  loops return every resource counter to baseline across generation reuse;
- K6 wait-multiple must cover 1- and 16-member sets, invalid count/alignment/
  user ranges, complete prevalidation of stale handles and rights, duplicate
  handles, deterministic lowest-ready index, event and semaphore consumption,
  thread-death detail, aggregate object-queue exhaustion, and publication
  rollback with no linked registration or deadline leak;
- every ordering of signal, thread death, timeout, cancellation, final close,
  creator death, and caller-process death must select one wait-set result,
  withdraw all nonwinning registrations exactly once, preserve readiness on
  nonwinning objects, and return all queue/deadline/registration counts to the
  baseline;
- target K6 scheduling must block one thread on at least two distinct handles,
  wake it through a nonzero member index, verify an immediate lowest-index
  winner and a thread-death detail, then pass fixed wait-set block/wake cycle
  budgets before publishing the K6 marker;
- K6 timers must cover rights/type failures, pool and owner-quota exhaustion,
  immediate expiry, equal-deadline slot ordering, rearm, cancel-before-expiry,
  cancel with waiters, retained fired state, final close, owner death, and exact
  return to heap/object/registration baselines;
- process-death waits must cover self rejection, live block, normal 32-bit exit
  detail, fault terminal result, immediate repeated observation, close before
  death, caller death, final reference release, generation reuse, and stale
  process-handle rejection;
- target scheduling must execute `THREAD_CREATE`, block in `WAIT_ONE` on the
  created thread, receive its exact exit status from `THREAD_EXIT`, repeat the
  wait as an immediate level-triggered result, close the handle, reject the
  stale handle, and reach a distinct K5 marker before kernel qualification;
- performance qualification must measure thread creation, thread-exit wakeup,
  and deferred thread reclamation separately, inspect generated MC68030 code,
  retain automated ceilings, and compare the exact Musashi workload and kernel
  image against the K4 baseline;
- aligned byte copy/clear primitives must cover every source/destination
  alignment and length through the longword fast path, with guard bytes proving
  no underrun or overrun;
- target scheduling must run two threads in one CRP and one thread in another,
  prove same-address-space switches do not increment VM/CRP switches, then
  fault and reap only the second process;
- the K6 release-development image must pass the complete pin-level SDRAM model
  and two independent ULX3S SRAM reloads of the exact qualified bitstream while
  matching the expected FPGA build ID and SD ROM CRC32;
- K6 must sample syscall, timer, user-fault, scheduler-pick, same/cross-CRP,
  block, wake, deadline expiry, thread create, thread exit, and deferred thread
  reap, wait-set block, and wait-set wake against the fixed budgets in
  `performance.h`, with at least one sample and zero overruns for every metric;
- the exact 1,000-cycle Musashi K6 lifecycle workload must complete at or below
  675,000,000 virtual machine cycles;
- a real M=1 interrupt must build exact format-0 MSP and format-1 ISP frames,
  preserve saved M, chain `RTE` through MSP, and restart a multiword
  instruction after clock-enable stalls;
- owner-ledger exhaustion, reuse, pinned-release atomicity, corrupt-link
  rejection, and release visits proportional only to the owner's frame count;
- all directed and integrated graphics coexistence tests.

Known upstream failures are tracked evidence, not waivers. A new mismatch is a
regression until classified against Motorola behavior and fixed or given a new
Motorola-corrected oracle.

## Change preflight

Before creating kernel code, search implementation, tests, and contracts for an
existing owner of the required behavior. The change review records which code
was reused or consolidated and why any new mechanism is necessary. New helpers
that merely duplicate generation handling, byte operations, queue state, or
accounting fail review.

For a hot path, capture target-representative cycles and kernel/image size before
editing, repeat the same workload afterward, and add an automated ceiling before
higher layers depend on it. Inspect generated MC68030 assembly as part of that
review. Hand assembly is accepted when it materially improves the measured path
without weakening the C-level contract or its tests.

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
- every deferred-worker state transition, retry wake, service-time signal, and
  return between guarded MSP, guarded ISP, and user mode;
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

Exact guarded-worker source `e108a3711befa08a309f068939dff226a21c869c`
repeats the bounded candidate gate on production build `25D9CB8E`. It reaches
cycle 5,000 after 302.531 host seconds and `0x00000000DFEAD7D7` coherent CPU
cycles, above the exact five-minute threshold. It reports 10,003 switches,
30,057 delivered ticks, syscall count `0xBE45`, exactly 7,987 free pages, and a
9,376-cycle maximum masked user-fault interval. Retained evidence is
`docs/evidence/k1-25d9cb8e-e108a37-candidate-5m-hw.log`, SHA-256
`781cd79f35e0b82c0c4e782864f3a7bfe7cfed405c8fe7fd974542b49c2cc3b5`.
The prior exact 30-minute routed-platform burn-in remains valid platform
history; it was not repeated for this source because the accepted per-change
gate is the bounded five-minute run.

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

The current K6 limits are 50,000 cycles for syscall and timer dispatch, 125,000
for user-fault containment, 10,000 for scheduler selection, 15,000 for same-CRP
switch, 50,000 for cross-CRP switch, and 15,000 each for block and wake. The
deadline expiry has a separate 20,000-cycle limit. Thread create, caller exit,
and deferred reap are limited to 150,000, 50,000, and 125,000 cycles. Wait-set
block and wake are each limited to 50,000 cycles. The exact K6 pin-level maxima
are 38,986, 27,656, 31,966, 1,506, 3,092, 4,327, 4,782, 7,888, 10,234,
124,512, 25,681, 28,879, 6,259, and 10,049 cycles respectively. That run
intentionally uses a 64 KiB simulated BIST; both routed-hardware runs execute
the real full-range 32 MiB BIST. Hardware run 1 reports wait-set block/wake
maxima of 6,267/10,045 cycles; run 2 reports 6,254/10,041. Every one of the
fourteen metrics has at least one sample and zero overruns in both runs.

Retained K3 evidence and SHA-256 values are:

- `docs/evidence/k3-8929c063-musashi-normal.log`:
  `33103a11ae413abfd4ce5ccb39b8e490621342a5ceacf334e779e9cb5362bd22`;
- `docs/evidence/k3-8929c063-musashi-performance.log`:
  `ece6fb827dff5d25527857922e94ec95935f68aed83b2a4a2fa7029bdb640076`;
- `docs/evidence/k3-8929c063-rtl.log`:
  `b2a24285eb4ec7fff3abdeaf1bef839ac98cf4bc35486de29f58d4667a203511`;
- `docs/evidence/k3-25d9cb8e-8929c063-hw-1.log`:
  `f05c5f0a6b88ab38fb3557d6f412dfbd708011b5078112eaa905b515a0856709`;
  and
- `docs/evidence/k3-25d9cb8e-8929c063-hw-2.log`:
  `f5f46ccd4230aca44a360a402dc57747e42aef2a8f56461f55960a3bd8ceaa55`.

Retained K4 pin-level and hardware evidence and SHA-256 values are:

- `docs/evidence/k4-662aa04-rtl.log`:
  `fa89ee4c9188866a20aed4ced11d90d7391a8455f0ef4fc1fd6614619ed661da`;
- `docs/evidence/k4-25d9cb8e-662aa04-hw-1.log`:
  `4dd8583781bca253229240e25f4169d70aaa1a5a224b22be24d4aef23d2c3135`;
  and
- `docs/evidence/k4-25d9cb8e-662aa04-hw-2.log`:
  `b614522dd02fd9f110b56196297dd7afb221165c60e5383424abd3a7e1139de6`.

Superseded/rejected K5 evidence and SHA-256 values are:

- `docs/evidence/k5-020f9460-musashi-normal.log`:
  `3e86cd1410b2846a1aa479db481def1d2c1b8fdfe25898fe076851f1079319d0`;
- `docs/evidence/k5-020f9460-musashi-performance.log`:
  `d8f8e71907b1c4442fb6ca28eadaa8e5fc46259740f290e900d4f6feba48c9f4`;
- `docs/evidence/k5-020f9460-rtl-create-budget-rejected.log`:
  `6d7ecd4e853c2865b1dad472cb6cf138b239f5924944d4dd7baf4469640a8972`;
- `docs/evidence/k5-020f9460-rtl.log`:
  `f609162ec62b0de53a1fe02e53f623e3cbd07195841c317cde98fcf2f5e9cf10`;
- `docs/evidence/k5-25d9cb8e-020f9460-provision.log`:
  `21d68093ecf4e77b420b3cc128ca01a1bf2ca6baae1b718dfe172a6776f1043e`;
- `docs/evidence/k5-25d9cb8e-020f9460-hw-1.log`:
  `c4ce877436276f4af55dfa983b789759b022b9fffbd3e26c43f4fb9cba68759f`;
  and
- `docs/evidence/k5-25d9cb8e-020f9460-hw-2.log`:
  `472589e0cdb827a5d8ec3d40db22af123024b5764c0727d4fe1fff53cff9d24d`.

Accepted K5 evidence and SHA-256 values are:

- `docs/evidence/k5-1a234d1e-musashi-normal.log`:
  `a8ca8dd640c4d10ca45d11b1957e798c193ff0a0a550af36a21f2bd0a9d20dc0`;
- `docs/evidence/k5-1a234d1e-musashi-performance.log`:
  `6f09f9175f8ec39a7f825bac7e701ed8c22cc517162f001c1ec01f90cbe726b7`;
- `docs/evidence/k5-1a234d1e-rtl.log`:
  `b26f7075305b23b1f445aeb2639d7d20bf22a7dfab8f5a5c8fa305a8253d66c1`;
- `docs/evidence/k5-25d9cb8e-1a234d1e-provision.log`:
  `8a380195cf4cab6a912f09a7a358b5a5a825006c3967b5c6bd9e5128303a0aca`;
- `docs/evidence/k5-25d9cb8e-1a234d1e-esp-production-flash.log`:
  `33f747b22720e471030cb568b09c7f66887a0c1a3301f91ae0ff2ca926e963b1`;
- `docs/evidence/k5-25d9cb8e-1a234d1e-hw-1.log`:
  `d915b9629ab0c77d3c8750e2b593eca7032329b0278a2d4cff87a9872e316d38`;
  and
- `docs/evidence/k5-25d9cb8e-1a234d1e-hw-2.log`:
  `c4a069f6cc2abccc09a86e815060ee66c314b518eaa91daf0420137e9914d753`.

Accepted K6 evidence and SHA-256 values are:

- `docs/evidence/astra68-k6-04524898-host.log`:
  `6d342c67418859ab8526852a5d76cb8fdc6bcf8171fa6104b91ce7814b194535`;
- `docs/evidence/astra68-k6-04524898-conformance.log`:
  `1e200cc385e29378041b5af4ff0352f2d5bab8c0a88cef7a99289a31ac5e699f`;
- `docs/evidence/astra68-k6-04524898-rust.log`:
  `5fc77ff33c7440a15171b1105572beefda4aa80ebed45e84facb40d2a9341b0b`;
- `docs/evidence/astra68-k6-04524898-vesta.log`:
  `bf9bfe75e7e2b8260dda2608d2d54148beec7e3047af88b427215e090cce416a`;
- `docs/evidence/astra68-k6-04524898-musashi-normal.log`:
  `bdd9700424646c59ab143f87a16c7901c0139d12117b587a8ce876e4261a2af5`;
- `docs/evidence/astra68-k6-04524898-musashi-performance.log`:
  `5f550a791b28b69bb98bd2defaa31ee211b4cec255ec221fdb9f7d1742af3c8d`;
- `docs/evidence/astra68-k6-04524898-rtl.log`:
  `73355c2eb5075df985a5aa4ec9b34ffbf5fb1d6e6fcb06f9a25d2f88bd080adc`;
- `docs/evidence/astra68-k6-04524898-provision.log`:
  `cf4937dc014b943c2f9e59a25920f9763b374784a5530216f51c8c78e998df37`;
- `docs/evidence/astra68-k6-04524898-esp-production-flash.log`:
  `dd92746edf357f7df851ecfa879f57955b76d0f3bb5609678f5313fcdf2b02d0`;
- `docs/evidence/astra68-k6-04524898-hw-1.log`:
  `1fb84795ee08f96735ed2625aacfd81f1afe58d5a56c0d46706e888d05a5ac96`;
  and
- `docs/evidence/astra68-k6-04524898-hw-2.log`:
  `77ce8067efabaf42c0541309eaa1fcf67264c1ee6dc1edb7ad10cf402e21f141`.

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
