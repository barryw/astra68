# Axiom locking and preemption contract

Status: normative single-core concurrency contract, revision 0.3 (2026-07-25)

This design is for one MC68030. It does not emulate SMP. Correct interrupt,
DMA, and MMIO ordering still applies even though only one CPU executes C code.

## Execution contexts

| Context | May allocate | May block | May take mutex | May copy user memory |
|---|---|---|---|---|
| hard IRQ top half | no | no | no | no |
| exception/syscall entry | emergency/bounded only | no | no | bounded copy only |
| scheduler tail | no | no | no | no |
| kernel/deferred thread | yes, fallible | yes | yes | only for its owning request |
| ordinary user thread syscall | yes, fallible | yes | yes | yes, before blocking/pin |
| panic path | no | no | no | no |

K7 retains one fixed stackful deferred worker. It runs with S=1/M=1 on a dedicated
8 KiB MSP behind an unmapped 4 KiB guard; exception, syscall, and hard-IRQ
entry use the separate guarded ISP. The worker enters at IPL 7, removes a
bounded work bitmap, enables interrupts around process reclamation, remasks
before changing state, and returns to a validated user context or executes an
atomic master-mode `STOP`. Vesta TIMER0 is one-shot: the hard timer
acknowledges, samples the monotonic cycle counter, expires bounded scheduler
deadlines, records quantum expiry, moves a bounded worker retry bit to ready
work, and requests scheduling. It never allocates or destroys an object.
All pending-work publication, including worker self-signaling, updates the
bitmap and `BLOCKED` to `READY` transition while interrupts are masked and then
restores the caller's exact prior interrupt state. Thus an interrupt cannot
publish a different work class between a worker load and store and have that
bit lost.

User-fault dispatch performs only fault classification, atomic transition to
`EXITING`, withdrawal from scheduling, selection of a runnable or empty CRP,
context replacement, and worker scheduling. It does not destroy page tables,
close handles, poison pages, or release owner frames while IPL 7 is held.
Those operations run later from the interruptible worker. Host tests assert
that neither process maintenance nor an owner release occurs during fault
dispatch. There is no lost-wakeup window: worker state changes to `BLOCKED`
with interrupts masked and `STOP #$3000` enables interrupts atomically.

An interrupt accepted while the worker runs in master mode uses the MC68030
format-0 MSP frame plus format-1 ISP throwaway frame. Entry masks IPL without
clearing M, and `RTE` returns through the already post-incremented MSP. Exact
Motorola-directed and full-system regressions cover this path.

The exact `bbb1616a1e65ef56619bffb11cb21e9ea1bc5202` hardware soak measured a
maximum user-fault dispatch interval of 8,834 CPU cycles, or 706.72 us at
12.5 MHz, well below one nominal timer period. The candidate gate rejects any
value above 125,000 cycles (10 ms). Musashi measured 4,482 cycles and the full
pin-level RTL model measured 8,866 cycles. This closes the prior unbounded,
multi-period IPL-7 teardown path; it does not turn ordinary teardown into hard
IRQ work.

## Preemption model

**CURRENT K7 substrate:** five process slots, 16 global thread slots, a
transitional limit of 15 threads/process, and a 5 ms one-shot quantum. At
12.5 MHz the quantum is exactly 62,500 CPU cycles. The scheduler has 32
intrusive FIFO ready queues and a 32-bit bitmap, selects the highest effective
priority in bounded constant steps, and rotates equal-priority threads
round-robin. A process is not a schedulable entity: it supplies a default
priority and policy ceiling, while each thread owns base and effective
priorities. Effective priority currently equals base priority because
inheritance and donation are not implemented. Each thread has an 8 KiB
supervisor stack behind its own 4 KiB unmapped guard.

All registers, USP, PC, and SR are thread state. CRP is process state. A switch
between threads in one process does not reload CRP or flush caches/ATC; host,
Musashi, full pin-level, and ULX3S tests count that path separately from a
cross-CRP switch. Timer and voluntary-yield paths apply priority selection.
The public event/semaphore/timer and death-wait paths prove immediate handoff
when a higher-priority waiter wakes or its deadline expires. K9 retains
transactional runtime thread creation, caller-only thread exit, waitable
thread/process death, bounded wait-multiple, and K7 message ports, and adds
waitable bulk-ring endpoints under provisional ABI `0x00010004`.

The running thread owns one absolute quantum deadline. Vesta is always
reprogrammed to the earlier of that deadline and the root of the wait-deadline
heap. A context activation starts a fresh quantum; an ordinary syscall does
not. Yield, blocking, exit, fault, and preemption select or enter another
context and therefore start that selected context's quantum. When every user
thread is blocked, the scheduler installs the empty CRP, clears the active
quantum, retains the one-shot deadline, and lets the guarded supervisor worker
idle. Expiry can make a thread ready from that state.

The wait-deadline queue is one fixed 16-entry binary min-heap, exactly one
entry per global thread slot. Deadlines are unsigned 64-bit absolute CPU-cycle
values. Equal deadlines are ordered by thread slot for deterministic behavior.
Four parallel arrays plus one count consume 258 bytes; insertion and removal
are `O(log 16)`, expiration is `O(expired * log 16)`, and no path allocates.
The implementation rejects an already-expired deadline before blocking and
cannot exceed the global thread limit.

**PLANNED stable scheduler work:**

- equal-priority/current-address-space affinity as the final tie-breaker;
- bounded interactive boost of at most two levels, decaying one level per full
  quantum and never entering real-time ranges;
- priority inheritance for kernel mutexes;
- explicit priority donation across one blocking RPC edge, removed on reply,
  timeout, cancellation, or peer death.

Priority bands are exact for ABI 0.1:

| Priority | Use |
|---:|---|
| 0 | idle only |
| 1-7 | background |
| 8-19 | ordinary |
| 20-23 | interactive/system services |
| 24-27 | time-sensitive media with budget right |
| 28-30 | privileged real-time |
| 31 | kernel deferred/critical work only |

A real-time thread has a replenished CPU budget. Exhausting it demotes the
thread to priority 23 until the next period, preventing starvation of input,
display, init, and debugger services.

## Nonpreemptible regions

Preemption disable is a nest-counted per-CPU scalar. IPL raising and preemption
disable are separate operations. Neither may span a wait, user copy, object
destructor, allocator slow path, device poll, or synchronous IPC.

`THREAD_CREATE` performs fallible allocation, clearing, mapping, and handle
reservation with interrupts enabled. It masks interrupts only for its bounded,
no-allocation commit: ready-queue publication plus process and scheduler
accounting. The mask closes the only single-CPU race with timer expiry. A host
hook injects a supervisor timer at the final enable-to-disable boundary and
requires both the expired waiter and created thread to remain linked exactly
once. Rollback runs interruptibly and is complete before returning an error.

Initial release budgets at 12.5 MHz are:

| Region | Hard maximum | Cycle equivalent |
|---|---:|---:|
| hard IRQ top half | 100 us | 1,250 cycles |
| interrupts disabled outside entry/exit | 200 us | 2,500 cycles |
| scheduler lock/preemption disabled | 200 us | 2,500 cycles |
| IRQ-safe raw lock | 25 us | 312 cycles |
| user-fault entry through replacement context | 10 ms | 125,000 cycles |

Instrumentation is active during boot. Debug builds panic on nesting underflow
or lock-order violations, and K1 qualification keeps collecting budget
overruns and maxima. A normal release freezes performance and IRQ-off sampling
when the initial supervisor reaches terminal stage, so steady-state syscall,
interrupt, and scheduler paths do not read the MMIO cycle counter merely for
instrumentation.

## Synchronization primitives

- `IrqGuard`: saves/restores SR/IPL for tiny CPU/IRQ shared state.
- `PreemptGuard`: prevents scheduler replacement on this CPU only.
- `RawLock`: nonblocking, IRQ-safe ownership assertion; it never spins waiting
  for a peer because no peer CPU exists.
- `Mutex`: PLANNED sleepable, priority-inheriting thread lock.
- `Event`: CURRENT K4 handle-backed auto/manual-reset object with bounded
  priority/FIFO wait and close/wake-all.
- `Semaphore`: CURRENT K4 handle-backed counted object with bounded direct
  waiter handoff and overflow rejection.
- `Timer`: CURRENT K7 handle-backed one-shot, level-triggered object with a
  fixed 32-entry deadline heap and deterministic slot tie-break.
- `WaitMultiple`: CURRENT K7 fixed 1-16 member registration set covering
  events, semaphores, timers, thread death, and process death.
- `Port`: CURRENT K7 bounded datagram endpoint with readable/writable queues,
  exact message/byte backpressure, peer-death wakeup, and atomic handle move.
- `WaitQueue`: CURRENT INTERNAL intrusive priority/FIFO blocked-thread queue
  with a nonzero sequence counter preventing a stale condition check from
  blocking.

There is no recursive mutex. Interrupt handlers never acquire `Mutex`.

## Lock ranks

Locks are acquired only in increasing rank:

| Rank | Class |
|---:|---|
| 10 | scheduler/run queue |
| 20 | process/thread state |
| 30 | process handle table |
| 40 | kernel object state/wait queue |
| 50 | address space/area map |
| 60 | physical frame allocator/commit account |
| 70 | IRQ/device/DMA queue |
| 80 | trace/diagnostic snapshot |

Destruction is two-phase: mark/withdraw under the object's lock, release all
locks, then invoke wakeups, child closes, device cancellation, and final free.
This prevents destructors from acquiring lower-ranked locks through callbacks.

## Atomic blocking rule

To prevent lost wakeups:

1. acquire the object's state lock;
2. test the condition and deadline;
3. link the current thread to its wait queue;
4. mark it `BLOCKED` while scheduler state is protected;
5. release object state and schedule;
6. a signal removes the waiter exactly once and changes it to `READY`.

Timeout, cancellation, signal, and peer death compete through one atomic waiter
state; exactly one wins and supplies the result.

The current single-CPU K7 synchronization, port, and thread-lifecycle paths run
inside serialized supervisor dispatch. They snapshot the wait-queue sequence
while testing the condition; blocking succeeds only if that sequence is still
current.
Every wake, expiry, close, or process retirement advances the sequence. A
successful timed block links the thread to the object wait queue first and
then to the deadline heap; failure unwinds both links before restoring
`RUNNING`. Signal/close removes the deadline before making the thread `READY`.
Expiry removes the heap entry and wait link before making it `READY`. Process
death performs the same withdrawal for every sibling before deferred
destruction. These paths are idempotence-checked so one race winner supplies
one result and one cancellation count. Explicit user cancellation and creator
death use this same owner. Wait-multiple validates every member before linking
a fixed registration row and installs at most one thread deadline. The winning
signal, timer expiry/cancel, target death, timeout, cancellation, close, owner
death, or caller retirement removes every nonwinning registration before
readiness. No path allocates or performs synchronous IPC.

K7 port endpoints reuse this exact owner. A receive handle samples the port's
readable queue sequence; a send handle samples its writable queue sequence.
Enqueue advances readable readiness, dequeue advances writable readiness, and
close/owner death advances and drains both. The raw send/receive syscalls do not
sleep. NDK deadline operations retry the raw call around K6 wait-one or
wait-multiple with one absolute deadline, so ports add neither timeout links nor
unchecked user pointers. A failed send captures the writable sequence in one
per-thread retry token. Its immediately following matching wait ignores generic
minimum-size readiness and blocks only if that exact sequence still holds;
otherwise it retries immediately. This is the lost-wakeup contract for a send
whose exact byte requirement exceeds currently free capacity. On the single
core, source-handle invalidation plus
FIFO publication and destination-handle publication plus FIFO removal are
short interrupt-masked, allocation-free commits after every fallible check and
user copy has completed.

## Interrupt and deferred work

A top half performs only:

1. read and acknowledge bounded status;
2. copy fixed state into a preallocated record;
3. mask a level source when required;
4. signal an IRQ endpoint/deferred worker;
5. request rescheduling.

Protocol parsing, queue draining, page release, mapping changes, and device
reset execute in a kernel thread or protected service. IRQ records have fixed
capacity; overflow masks the source, increments a counter, and wakes the owner
with an overflow/error event.

## Required measurements

The trace ring target records interrupt-disabled time, scheduler-lock time,
raw-lock time, wake-to-run latency, same-CRP switch cycles, cross-CRP switch
cycles, deadline-expiry cycles, ATC flushes, and real-time budget exhaustion.
Current K6 qualification enforces fourteen bounded path measurements. The
final full pin-level run measured 38,986 syscall, 27,656 timer, 31,966 user
fault, 1,506 scheduler pick, 3,092 same-CRP, 4,327 cross-CRP, 4,782 block,
7,888 wake, 10,234 deadline, 124,512 create, 25,681 exit, 28,879 reap, 6,259
wait-set block, and 10,049 wait-set wake cycles. Two independent production
build `25D9CB8E` boots measured the same ordered paths as
38,983/27,644/32,005/1,508/3,095/4,314/4,781/7,865/10,240/124,516/25,664/
28,873/6,267/10,045 and
38,975/27,666/31,972/1,508/3,095/4,318/4,786/7,881/10,239/124,464/25,654/
28,879/6,254/10,041 cycles. Every value remains below its fixed limit and
every sampled overrun counter is zero. Generic maximum
interrupt-disabled, scheduler-lock, raw-lock, and wake-to-run instrumentation
is still MISSING and must not be inferred from these path measurements.
