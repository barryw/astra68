# Axiom shared areas and bounded bulk IPC rings

Status: K8 locked implementation contract, revision 0.1 (2026-07-25)

K7 message ports remain the control plane. K8 adds explicitly mapped shared
areas and bounded single-producer/single-consumer rings for payloads that
should not be copied through the kernel. A ring notification is a doorbell,
not a payload transfer: applications may commit an arbitrary bounded batch
before issuing one notification.

## Fixed limits and charges

The K8 development profile uses these exact limits:

| Resource | System | Per creator | Per object/process |
|---|---:|---:|---:|
| live areas | 8 | 4 | one fixed logical slot |
| committed area pages | 128 (512 KiB) | 64 (256 KiB) | 1-16 (4-64 KiB) |
| live area mappings | 32 | n/a | 4/process, one/process/area |
| live rings | 16 | 4 | 4/area |
| ring endpoints | 32 | n/a | one producer and one consumer |
| ring waiters | existing fixed wait pool | n/a | 16/endpoint |
| ring element size | n/a | n/a | 4-4096 bytes, four-byte aligned |
| ring capacity | n/a | n/a | 2-1024, power of two |

An area is charged to its creating process before publication. Physical pages
are allocated from real commit, zeroed, and owned by a generation-derived area
owner ID rather than by either mapping process. There is no overcommit. Page
tables remain charged to the process whose CRP contains them. Ring payload
storage is part of its area charge; a ring allocates no second payload queue.

All object records, mapping records, wait queues, and ring records come from
fixed pools. Create, map, unmap, notify, wait, close, timeout, cancellation,
peer death, and process death use no general heap.

## Logical mapping contract

The area window is `0x40000000..0x4007FFFF`, divided into eight 64 KiB slots.
Area slot `n` always maps at `0x40000000 + n * 0x10000` in every process. A
process may map an area once. The kernel selects the address; callers cannot
request an alias or collide with code, stacks, guards, MMIO, or another area.

CPU-only shared areas are cacheable and write-through. This is safe only under
all of these locked invariants:

1. every alias of one area page has the same logical address;
2. the machine is single-core;
3. switching CRPs invalidates both logical caches before loading the new CRP;
4. the kernel never accesses a published area through the supervisor physical
   alias except during a transition bracketed by full cache invalidation; and
5. no FPGA engine, ESP, USB controller, graphics engine, or other bus master
   accesses a CPU-shared area.

The VM records a mapping count and logical-slot class for every user-mapped
frame. A private frame permits one mapping. A shared frame permits at most one
mapping per process and rejects a different logical-slot class. Hardware/DMA
memory remains cache-inhibited or uses a separately qualified ownership and
cache-maintenance protocol; K8 area handles cannot request that policy.

Mapping an area is one transaction:

1. validate the live handle, `map` plus requested `read`/`write` rights,
   range arithmetic, process mapping quota, and fixed slot;
2. reserve one unpublished mapping record;
3. allocate and zero at most one missing PMMU leaf;
4. retain every area frame and validate every destination descriptor;
5. publish all descriptors;
6. invalidate logical caches and execute one full ATC flush;
7. publish the mapping record and charge.

Failure unwinds every retained frame and unpublished leaf. No partial mapping
is reachable. Repeating an identical map is idempotent and returns the same
base and size; requesting different permissions for an existing mapping is an
access error.

Unmap clears the complete range, performs one cache invalidation and ATC flush,
then drops frame and mapping references. Area close or creator death revokes
every mapping, including mappings in foreign CRPs, before another user process
can execute. Process death also removes that process's mappings of foreign
areas before its generic address-space destruction.

## Area object and handle lifetime

The internal area record contains:

```text
generation, creator process ID, frame-owner ID
byte size, page count, fixed logical slot
handle references, child references, mapping references
state, corruption latch, physical page[16]
```

The state machine is:

```text
FREE -> RESERVED -> LIVE -> CLOSING(terminal) -> FREE(new generation)
```

Only `LIVE` accepts a duplicate, child ring, or mapping. Handle duplication may
only reduce rights and increments the handle reference before publication.
Moving a handle through a K7 message changes no object reference count.
Mappings and child rings each own explicit references.

The final handle does not close an area while a live child ring still owns it.
When the final handle and child reference disappear, or when the creator dies,
the area enters terminal `CLOSING`, rejects new work, revokes all mappings, and
releases its base frame references. The slot is reusable only after all stale
handles and child rings have released their references. Creator death overrides
foreign handles and child references; those handles remain closeable but all
operations return `PEER_DEAD`.

## Ring ABI

One ring is unidirectional and has exactly one producer endpoint and one
consumer endpoint. Full-duplex protocols place two rings in one area. The
producer and consumer endpoints are independently transferable through K7
messages. A typical setup message atomically transfers a reduced-right area
handle and one ring endpoint.

Every ring begins at a 64-byte-aligned area offset and uses this exact 64-byte
native-big-endian header:

| Offset | Width | Field | Ownership |
|---:|---:|---|---|
| `0x00` | 4 | magic `0x4152494E` (`ARIN`) | immutable |
| `0x04` | 2 | ABI version `1` | immutable |
| `0x06` | 2 | header size `64` | immutable |
| `0x08` | 4 | flags, currently zero | immutable |
| `0x0C` | 4 | element size | immutable |
| `0x10` | 4 | capacity | immutable |
| `0x14` | 4 | data offset `64` | immutable |
| `0x18` | 4 | total ring bytes | immutable |
| `0x1C` | 4 | nonzero ring generation | immutable |
| `0x20` | 4 | producer position | producer only |
| `0x24` | 12 | reserved zero | immutable |
| `0x30` | 4 | consumer position | consumer only |
| `0x34` | 12 | reserved zero | immutable |

The ring occupies `64 + element_size * capacity` bytes, checked without
overflow and entirely contained in the area. Rings in one area cannot overlap.
Positions are monotonically advancing unsigned 32-bit element counts. Used
elements are `producer - consumer`; a value greater than capacity is corrupt.
Power-of-two capacity makes the slot index `position & (capacity - 1)` and
keeps wrap behavior defined while capacity remains below `2^31`.

Producer publication order is payload write, release fence, producer-position
write. Consumer order is producer-position read, acquire fence, payload read,
then release fence and consumer-position write. The MC68030 release fence emits
a compiler memory barrier plus the documented `NOP` needed to complete prior
external writes before publishing the position. The NDK never uses C bitfields,
packed structures, raw atomics requiring `libatomic`, or host-endian wire data.

The NDK view keeps one process-local outstanding reservation token. Reserving
when full or consuming when empty returns `WOULD_BLOCK` without changing shared
state. Commit advances exactly one position. Applications reserve and commit a
batch locally, then call `astra_bulk_ring_notify()` once; there is no required
syscall per element.

## Ring object, waiting, and failure

The kernel stores immutable ring configuration, shadow producer/consumer
positions, endpoint references, two intrusive wait queues, terminal results,
and an area child reference. It never copies ring payloads and never reads a
live ring through the supervisor physical alias. Ring creation formats the
header during a cold cache-synchronized transition only after validating the
complete region, then reserves both endpoint handles atomically.

`WAIT_ONE` and `WAIT_MULTIPLE` accept ring endpoints:

- a producer endpoint is ready while the consumer exists and the ring is not
  full;
- a consumer endpoint is ready while data exists;
- after producer death, a consumer may drain committed elements and then gets
  `PEER_DEAD`;
- consumer death makes the producer immediately `PEER_DEAD`;
- area revocation makes both endpoints immediately `PEER_DEAD`.

`RING_NOTIFY` submits the endpoint's expected role and only the caller-owned
monotonic position. The kernel rejects a role/handle mismatch, then
validates it against canonical capacity and its shadow opposite position,
returns both canonical shadow positions, advances the opposite wait-queue
sequence, and wakes all opposite waiters whose condition became true. It does
not dereference shared memory on this hot path. Each NDK endpoint batches
against the last returned opposite shadow position; it never treats a peer's
unnotified shared-header position as kernel-published. A no-advance notification
is a bounded position refresh and does not wake waiters.
Wait preparation rechecks shadow state and records the queue sequence before
linking, so a notification between a failed local probe and the wait syscall
cannot be lost. Forgetting to notify cannot corrupt the ring; the unnotified
batch is not kernel-published and may leave the peer asleep until another
notification, cancellation, close, or finite deadline.

The NDK validates immutable fields, reserved bits, and position distance before
every reservation and commit. It reports detected corruption through a failing
notification; an impossible submitted position is independently rejected by
the kernel. Either case closes only that ring with `IO_ERROR`, wakes both
endpoint queues once, and increments a corruption diagnostic. Untrusted
shared-memory corruption never panics the kernel. Internal reference,
generation, queue, or pool corruption is still an invariant failure.

The endpoint state machine is:

```text
OPEN -> PRODUCER_CLOSED or CONSUMER_CLOSED -> CLOSING -> FREE
  |---------------- area/creator death ----------------^
```

Endpoint close, process death, cancellation, timeout, and notification are
serialized terminal contenders through the existing wait implementation. Ring
reuse advances a nonzero generation, and a stale endpoint cannot name the new
ring.

## Syscall and lock-order contract

K8 adds `HANDLE_DUPLICATE`, `AREA_CREATE`, `AREA_MAP`, `AREA_UNMAP`,
`RING_CREATE`, and `RING_NOTIFY`. Create/map outputs publish atomically. User
pointers are never retained. `AREA_CREATE` and `AREA_MAP` may enable interrupts
during bounded preparation, but supervisor timer handling cannot switch away
from their in-progress unpublished transaction.

The single-core serialization order is:

```text
process handle table -> ring object -> area object -> address-space VM
                     -> physical-frame allocator
```

Handle release callbacks run only after the handle entry has been invalidated.
Ring code never enters a handle table. Area code never enters a ring object.
No path sleeps while holding any of these serialized states, and no synchronous
IPC occurs inside them.

## Acceptance evidence

K8 is not complete until exact host, Musashi, pin-level RTL, and ULX3S evidence
proves:

- allocation failure at every area create/map publication point rolls back to
  the exact frame, mapping, object, handle, and page-table baseline;
- stale and reduced-right handles cannot map, administer, or transfer more
  authority than granted;
- different-VA cached aliases, overlap, arithmetic wrap, oversized rings, and
  duplicate process mappings are rejected;
- full/empty/wrap operation, batched notification, missed-wakeup races,
  wait-multiple, endpoint close, owner death, peer death, and corrupt headers
  have exact terminal results;
- repeated create/map/ring/transfer/unmap/process-death cycles show no
  monotonic frame, mapping, handle, endpoint, waiter, or byte growth;
- area map, area unmap, ring notify, wait, context switch, syscall, and fault
  paths have fixed cycle budgets with zero overruns; and
- two independent hardware boots use the exact qualified ROM identity while
  the production FPGA bitstream, routed resources, and clock results remain
  unchanged.

The current release candidate passes all 20 host suites normally, under GCC
ASan/UBSan, and under GCC `-fanalyzer`; all NDK host, sanitizer, MC68030, and
generated HTML/PDF documentation gates; normal Musashi; and its exact
1,000-iteration performance workload. That workload completes in 576,508,511
cycles against the unchanged 675,000,000-cycle ceiling, retains 7,986 free
pages, and has zero performance overruns. A clean, non-reused Verilator 5.047
build passes the pin-level 64 KiB SDRAM test, every K1-K8 marker, exact object
cleanup, and all 20 cycle limits. The measured K8 pin-level maxima are 37,787
cycles for create, 56,267 for map, 71,295 for unmap, and 29,337 for notify.
Source freeze, SD provisioning, and two independent ULX3S boots remain open;
therefore K7 remains the hardware-qualified rollback release.
