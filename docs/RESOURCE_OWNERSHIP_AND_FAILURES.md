# Axiom resource ownership and failures

Status: normative lifetime contract, revision 0.2 (2026-07-25)

Every resource has one accountable owner, finite capacity, explicit rights,
and a terminal failure path. C cleanup helpers improve normal code but kernel
ownership remains the backstop for crashes and forced termination.

## Ownership model

- A process owns a resource account, address space, threads, handle table,
  committed pages, pinned pages, and queued-byte charge.
- A kernel object owns its internal storage and references to child objects.
- A handle owns one reference plus rights; closing it consumes that reference.
- A shared area owns physical frames; mappings own references to the area, not
  independent claims on its frames.
- A service owns device policy. The kernel owns IRQ masking, MMIO delegation,
  DMA validation, cancellation, and reset authority.
- An asynchronous request owns its queue slot, transaction/fence number,
  timeout, cancellation state, and pinned-memory references until one terminal
  completion wins.

No global index, PID, address, port name, or MMIO channel number grants access.

## Generic object lifecycle

```text
FREE --create--> LIVE --last-close/kill--> CLOSING --drain--> DEAD
  ^                                                        |
  +---------------- generation increment + reuse ----------+
```

Rules:

1. `LIVE` is the only state accepting new handles, waits, mappings, or work.
2. Entering `CLOSING` is atomic and idempotent.
3. Entering `CLOSING` withdraws lookup/publication before callbacks run.
4. All waiters wake with a documented close, cancel, or peer-dead result.
5. In-flight hardware work becomes `REVOKING`; memory stays pinned until the
   matching completion, abort, timeout/reset, or generation change retires it.
6. `DEAD` has no queue entries, mappings, waiters, pins, or external handles.
7. Reuse increments a nonzero generation before publication.

Reference count overflow, underflow, double completion, and generation mismatch
are invariant failures. Untrusted stale operations return an error; corruption
found wholly inside the kernel panics with an object dump.

## Process termination

Termination is a bounded state machine:

```text
RUNNING -> EXITING -> THREADS_STOPPED -> HANDLES_CLOSED
        -> DEVICES_REVOKED -> VM_DESTROYED -> ACCOUNT_EMPTY -> DEAD
```

Exact order:

1. prevent creation of new threads, handles, waits, mappings, and submissions;
2. remove every thread from ready/wait queues and record its terminal reason;
3. atomically withdraw handles, then invoke release callbacks without the
   handle-table lock;
4. cancel queued device work and mark active DMA requests revoking;
5. wake IPC peers and return uncommitted transferred handles;
6. switch away from the address space, unmap areas, and flush required ATC and
   cache state;
7. retain pinned frames until matching completion or reset retires them;
8. release all remaining owned frames and quota charges;
9. mark `DEAD` and make the diagnostic death record observable.

The process may remain `EXITING` while a device owns pinned memory. This is not
a leak: the request has a finite deadline/reset path and an inspectable owner.
K1's fixed deferred worker records one retry bit, blocks without allocating,
and retries after the next timer interrupt. DMA completion and worker
state-machine host tests prove that the process remains inspectable and is
reaped exactly once after the final pin retires.

The current K6 split implements the first and last parts of this order with
separate bounded objects. Marking a process `EXITING` removes every one of its
`CREATED`, `READY`, `RUNNING`, or `BLOCKED` threads from scheduling, removes
each timed waiter from the fixed deadline heap, and marks them `DEAD` in one
bounded 16-slot pass. Every affected wait-queue sequence advances, so a stale
condition snapshot cannot relink the retiring thread. Their generation records
remain occupied while handles, mappings, and owner frames are reaped by the
guarded worker; only successful final reap releases those slots for
generation-safe reuse. A fault in one process therefore cannot leave a runnable
sibling thread or recycle a thread record while a stale process-private handle
still exists.

K1's physical allocator has 64 fixed owner ledgers. Every dynamically allocated
frame is linked into exactly one owner's intrusive list using 16-bit previous
and next indexes; no teardown metadata is allocated dynamically. Owner release
first walks only that owner's frames to validate that none are pinned, then
walks the same list to poison and release them. A pinned frame returns `BUSY`
without releasing any frame. Corrupt links return `INVALID_MAP`; exhausting all
64 ledgers fails allocation before publication. An empty ledger is immediately
reusable. Release work is therefore O(frames owned), never O(all 8,192 physical
frames), and diagnostics expose owner slots, release operations, and exact
frame visits.

## Thread creation and death

K5 thread creation is one transaction with this reservation order:

1. reserve an unpublished global thread record and guarded supervisor-stack
   slot;
2. reserve one free process stack slot and one physical user-stack frame;
3. clear the frame and map it read/write above an unmapped lower guard;
4. install one generation-safe process-local thread handle with the requested
   subset of `read`, `wait`, and `administer`;
5. initialize the saved context and publish the ready thread;
6. commit the thread-object, user-stack, user-guard, supervisor-stack, and
   supervisor-guard charges.

Steps 1 through 4 and context initialization are an interruptible preparation
phase. The handle slot is reserved in the caller's table but cannot be observed
by user code because the creating syscall has not returned. Steps 5 and 6 are a
no-allocation commit with interrupts masked, including ready-queue insertion and
all process/scheduler accounting. Failure before commit unwinds 4 through 1.
Rollback is an invariant, not best effort: a failed rollback is kernel
corruption. No failed creation changes any committed quota counter,
live-thread count, ready queue, mapping count, handle count, or physical-frame
baseline. Once commit begins, a failed publication invariant is kernel
corruption rather than a recoverable partial transaction.

The mask is required even on one CPU: a supervisor timer can expire a waiter
and enqueue it while ordinary user code is in `THREAD_CREATE`. A deterministic
test injects that timer immediately before the commit mask and proves the
expired waiter and newly published thread are both linked exactly once.

A published thread has one execution reference and zero or more handle
references. ABI 0.2 creates one handle and has no duplicate/transfer operation,
so K5 normally has zero or one handle reference. Closing the final handle wakes
death waiters with `CLOSED` but leaves a live execution reference untouched.
Normal `THREAD_EXIT` records the status once and releases the execution
reference. The deferred worker releases the user-stack mapping; the thread
record and supervisor stack become reusable only after both death and final
handle close. Generation advances before either handle-table or thread-record
reuse, so stale handles cannot name a replacement thread.

The single-core transition ordering is exact: syscall entry has interrupts
masked; it records terminal state and removes/wakes wait links before making a
replacement thread current. The worker performs mapping destruction only
after execution has moved to its dedicated MSP and enables interrupts around
that bounded maintenance pass. Signal, deadline, cancellation, handle close,
thread exit, and process death therefore cannot each complete the same wait.

## Rights

Every handle entry records object pointer, generation, type, rights, and one
reference. Generic rights are read, write, map, signal, wait, transfer,
administer, and debug. Operations request the exact subset they require.

Duplicate may only reduce rights. Transfer requires `transfer` and cannot add
rights. Administer and direct device mapping are never inherited implicitly.

## Event, semaphore, and timer ownership

The initial production synchronization pool has 32 fixed slots, an eight-object
creator quota, and at most 16 waiters on one object. Events, semaphores, and
timers share this pool, one lifecycle implementation, and the scheduler's
existing intrusive wait/deadline links. Armed timers use parallel fixed
32-entry heap/position/deadline arrays. No wait, signal, set, cancel, close,
timeout, expiry, or owner-death path allocates memory.

A live handle contributes one object reference. The final handle close changes
`LIVE -> CLOSING`, records `CLOSED`, withdraws future operations, removes every
waiter from both scheduler queues, and wakes each once. Creator death performs
the same transition with `PEER_DEAD` after its own threads have been retired.
The slot returns to `FREE` only when references and waiters are both zero. A
new allocation advances the slot generation before publishing its first
handle.

Cancellation is addressed through a process-local thread handle carrying the
cancel-wait right. It succeeds only while that thread is blocked. Signal,
deadline, cancel, close, and owner death are serialized terminal contenders:
the first one removes both links and writes the result; every later contender
observes a nonblocked thread or closing object and cannot overwrite it.

K6 wait sets do not own object references or user memory. A blocked thread owns
one fixed registration for each copied descriptor and at most one deadline
entry. The current pool contains 256 registrations, statically partitioned as
16 members for each of 16 thread slots, so one process cannot consume another
thread's registration reserve. Completion withdraws the complete row before
publishing the thread as ready. A final handle close remains safe because it
first closes the object and completes all registrations that still name its
queue; no blocked wait retains an unchecked handle-table entry or user pointer.

Process and thread death queues are embedded in their generation-stable object
records. A published process or thread owns one execution reference plus zero
or more handle references. Death drops the execution reference, records one
terminal result/detail, wakes the embedded queue, and leaves a bounded zombie
only while a handle still owns observation rights. Final handle close wakes any
remaining foreign waiter with `CLOSED`; it never terminates live execution.
Object reuse is forbidden until execution, handle references, and death waiters
are all zero. A process-private numeric ID is never authority.

Timer set replaces any existing arm only after handle/type/right validation.
Expiry removes the timer heap entry before setting level readiness and
withdrawing all wait registrations. Cancel removes the heap entry, clears
readiness, and completes current waiters with `CANCELLED`. Final close and
creator death use the common closing transition and cannot leave an armed heap
slot.

Semaphore release validates the entire operation before waking anyone. It
computes direct handoffs first, rejects a release whose remainder would exceed
the configured maximum, then commits all wakeups and the residual count. A
failed release therefore has no partial effect.

## IPC ownership

Ports have limits in both messages and bytes. The initial defaults are 64
messages/port, 64 KiB/port, and 256 KiB charged to one process. A send reserves
message bytes, receiver queue position, and all handle destinations before
commit. Failure before commit changes no ownership.

Terminal endpoint behavior:

- blocked senders and receivers wake `PEER_DEAD`;
- queued copied messages are freed and their byte charges returned;
- synchronous transactions complete once with `PEER_DEAD`;
- uncommitted handle transfers remain with the sender;
- committed handles close through receiver teardown;
- shared rings remain valid only while their area handles remain live.

## Device and DMA ownership

Each operation carries:

- process/service owner;
- queue slot generation;
- device generation;
- monotonically advancing sequence/fence;
- physical base, checked byte length, direction, and cache policy;
- absolute monotonic deadline;
- completion state and cancellation reason.

The kernel validates address addition, alignment, wrap, frame ownership,
mapping policy, and pin budget before programming hardware. A stale completion
cannot complete a reused request because both slot and device generations must
match.

Service death masks its IRQs, rejects new submissions, cancels queued work,
requests bounded hardware stop, advances device generation, and resets on
timeout. Clients receive `PEER_DEAD` or `CANCELLED`; unrelated devices and
processes continue.

The K1 `block.c` path is ownership/revocation qualification plumbing, not the
permanent filesystem or block policy. Those protocols move to a user service.

## Allocation and queue failures

| Failure | Required behavior |
|---|---|
| object pool exhausted | return `NO_RESOURCES`; no partial publication |
| commit/page unavailable | return `OUT_OF_MEMORY`; unwind all reservations |
| handle table full | leave object/reference with caller |
| port full | block to deadline or return `WOULD_BLOCK` |
| wait set too large | return `INVALID_ARGUMENT` before linking waiters |
| pin budget exceeded | reject before device submission |
| peer dies | wake once with `PEER_DEAD` |
| timeout races completion | one terminal state wins; loser observes it |
| stale handle/completion | return stale/invalid and increment diagnostic count |
| physical bus failure | fail request; reset/mask device when required |
| internal impossible state | panic with retained object/owner trace |

No recoverable external failure returns success, grows a queue, or spins
indefinitely.

## Low-memory ownership

There is no overcommit. Every page is charged before publication. A fixed
emergency reserve is owned by the kernel and may only finish a fault, IPC
cleanup, process death, panic logging, or debugger operation. It may not satisfy
ordinary application allocation.

Pressure order is cache reclamation, service notification, rejection of
nonessential allocation, preservation of input/display/init/debugger, then a
documented non-system victim only as a final policy action by the system
service. The kernel never chooses a victim through hidden heuristics.

## Diagnostics

For every live or closing object the monitor can report type, generation,
owner, rights, state, references, waiters, queue depth/bytes, pins, deadline,
last completion/error, and creation site tag. Teardown tests compare all pool,
frame, pin, and byte counters with a captured baseline.
