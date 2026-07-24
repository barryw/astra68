# Astra 68 resource ownership and failures

Status: normative lifetime contract, revision 0.1 (2026-07-23)

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

The current K2 split implements the first and last parts of this order with
separate bounded objects. Marking a process `EXITING` removes every one of its
`CREATED`, `READY`, `RUNNING`, or `BLOCKED` threads from scheduling and marks
them `DEAD` in one bounded 16-slot pass. Their generation records remain
occupied while handles, mappings, and owner frames are reaped by the guarded
worker; only successful final reap releases those slots for generation-safe
reuse. A fault in one process therefore cannot leave a runnable sibling thread
or recycle a thread record while a stale process-private handle still exists.

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

## Rights

Every handle entry records object pointer, generation, type, rights, and one
reference. Generic rights are read, write, map, signal, wait, transfer,
administer, and debug. Operations request the exact subset they require.

Duplicate may only reduce rights. Transfer requires `transfer` and cannot add
rights. Administer and direct device mapping are never inherited implicitly.

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
