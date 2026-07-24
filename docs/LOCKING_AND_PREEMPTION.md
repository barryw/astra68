# Astra 68 locking and preemption contract

Status: normative single-core concurrency contract, revision 0.1 (2026-07-24)

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

K1 has one fixed stackful deferred worker. It runs with S=1/M=1 on a dedicated
8 KiB MSP behind an unmapped 4 KiB guard; exception, syscall, and hard-IRQ
entry use the separate guarded ISP. The worker enters at IPL 7, removes a
bounded work bitmap, enables interrupts around process reclamation, remasks
before changing state, and returns to a validated user context or executes an
atomic master-mode `STOP`. The 100 Hz hard timer only acknowledges, records
the tick, moves a bounded retry bit to ready work, and requests scheduling.

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

**CURRENT K2 substrate:** four process slots, 16 global thread slots, a
transitional limit of 15 threads/process, and a 100 Hz periodic timer with a
10 ms nominal quantum. The scheduler has 32 intrusive FIFO ready queues and a
32-bit bitmap, selects the highest effective priority in bounded constant
steps, and rotates equal-priority threads round-robin. A process is not a
schedulable entity: it supplies a default priority and policy ceiling, while
each thread owns base and effective priorities. Effective priority currently
equals base priority because inheritance and donation are not implemented. Each
thread has an 8 KiB supervisor stack behind its own 4 KiB unmapped guard.

All registers, USP, PC, and SR are thread state. CRP is process state. A switch
  between threads in one process does not reload CRP or flush caches/ATC; host,
  Musashi, full pin-level, and ULX3S tests count that path separately from a
  cross-CRP switch. Timer and voluntary-yield paths apply priority selection.
  The internal K2 event path proves immediate handoff when a higher-priority
  waiter wakes. Thread creation and event syscalls remain qualification-only;
  no stable user synchronization ABI is exposed.

**PLANNED stable scheduler work:**

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
| user-fault entry through replacement context | 10 ms | 125,000 cycles |

Debug builds timestamp every enter/leave and panic on nesting underflow or lock
order violation. Release builds count budget overruns and expose the maximum;
repeated overruns fail qualification.

## Synchronization primitives

- `IrqGuard`: saves/restores SR/IPL for tiny CPU/IRQ shared state.
- `PreemptGuard`: prevents scheduler replacement on this CPU only.
- `RawLock`: nonblocking, IRQ-safe ownership assertion; it never spins waiting
  for a peer because no peer CPU exists.
- `Mutex`: PLANNED sleepable, priority-inheriting thread lock.
- `Event`: CURRENT INTERNAL level object with consume-on-wait and close/wake-all.
- `Semaphore`: PLANNED counted wait object.
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

The current single-CPU qualification event runs inside serialized supervisor
dispatch. It snapshots the wait-queue sequence while testing the condition;
`kernel_thread_block` succeeds only if that sequence is still current. Every
wake or removal advances the sequence. This proves the condition-change/block
boundary without introducing a second queue implementation; object locks and
the four-way timeout/cancel/signal/death arbitration remain stable-ABI work.

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
