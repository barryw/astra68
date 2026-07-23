# Astra 68 locking and preemption contract

Status: normative single-core concurrency contract, revision 0.1 (2026-07-22)

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

K1 has no sleepable kernel worker yet. It drains bounded process teardown at a
syscall safe point or from supervisor idle. Syscall entry temporarily enables
interrupts around maintenance and remasks them before reading or replacing the
saved user context. The 100 Hz hard timer path only acknowledges, records the
tick, and requests scheduling.

**CURRENT K1 violation:** user-fault retirement calls synchronous address-space
destruction and owner-frame release before leaving the IPL-7 exception entry.
The physical lifecycle run demonstrated that this spans multiple 10 ms timer
periods. Vesta coalesces those expirations into one pending bit. K1 release
therefore requires fault entry to mark the process dead, select a runnable
context, and queue reclamation only; page-table walks, poisoning, handle close,
and frame release run later with interrupts enabled in bounded deferred work.

## Preemption model

**CURRENT K1:** four process slots, one thread each, 100 Hz periodic timer,
round-robin ready selection, and a 10 ms nominal quantum. All registers, USP,
PC, SR, and CRP are switched. A same-address-space optimization is not yet
needed because K1 has one thread per address space.

**PLANNED stable scheduler:**

- 32 fixed priorities and one 32-bit ready bitmap;
- O(1) highest-priority selection and intrusive FIFO queues;
- round-robin within one priority;
- initial ordinary quantum 5 ms using a one-shot timer;
- immediate reschedule when a higher-priority thread becomes ready;
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

Initial release budgets at 12.5 MHz are:

| Region | Hard maximum | Cycle equivalent |
|---|---:|---:|
| hard IRQ top half | 100 us | 1,250 cycles |
| interrupts disabled outside entry/exit | 200 us | 2,500 cycles |
| scheduler lock/preemption disabled | 200 us | 2,500 cycles |
| IRQ-safe raw lock | 25 us | 312 cycles |

Debug builds timestamp every enter/leave and panic on nesting underflow or lock
order violation. Release builds count budget overruns and expose the maximum;
repeated overruns fail qualification.

## Synchronization primitives

- `IrqGuard`: saves/restores SR/IPL for tiny CPU/IRQ shared state.
- `PreemptGuard`: prevents scheduler replacement on this CPU only.
- `RawLock`: nonblocking, IRQ-safe ownership assertion; it never spins waiting
  for a peer because no peer CPU exists.
- `Mutex`: sleepable, priority-inheriting thread lock.
- `Event/Semaphore`: waitable count or level object with atomic test-and-block.
- `WaitQueue`: intrusive, priority-ordered blocked-thread list.

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

The trace ring records interrupt-disabled time, scheduler-lock time, raw-lock
time, wake-to-run latency, same-CRP switch cycles, cross-CRP switch cycles,
ATC flushes, and real-time budget exhaustion. `STATUS.md` must label each as
unmeasured until hardware data exists.
