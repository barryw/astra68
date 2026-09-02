# Astra filesystem concurrency design

Status: implementation in progress; Phase D read concurrency and mutation
linearization checkpoints pass, as does the first physical Phase E
dirty-shutdown checkpoint

This document defines what Astra means by a multi-threaded filesystem and the
minimum implementation that may claim it. It extends
[`STORAGE_AND_VFS.md`](STORAGE_AND_VFS.md); that document remains authoritative
for namespace, media, persistence, resource, and block-device behavior.

## 1. Goal

One slow filesystem request must not stop unrelated filesystem work.

On Astra's single MC68030, concurrency means several requests may be in
progress while only one thread executes instructions at an instant. When one
request waits for block I/O or a filesystem lock, another ready request can
use the CPU. This is useful without SMP and is the behavior early single-CPU
Unix systems described as a multi-threaded filesystem.

The first required proof is exact:

1. client A starts a cache-missing read and the test backend holds its block
   completion;
2. client B starts an unrelated read whose data and metadata are already in
   cache;
3. B completes before the test releases A;
4. A then completes with the correct bytes;
5. no extra physical read, leaked request, lost reply, or filesystem error is
   permitted.

A worker pool that blocks behind one mount-wide lock fails this proof and is
not the feature.

Performance is the product requirement, not an optional follow-up. In steady
state, a cached operation must wait only for a true conflict on the same
object. It must not wait for unrelated I/O, another directory, another inode,
the journal when it is only reading clean data, or a service-wide allocation.
The device queue stays full when useful I/O exists, and the CPU stays on ready
filesystem work while any request waits for media.

Blocking is still the correct action when a request has no useful work until a
dependency completes. Such a thread sleeps immediately; it never polls or
spins. “Minimal blocking” means removing false dependencies and lock
contention, not burning the only CPU while waiting for physics.

## 2. Current implementation

The present stack admits several synchronous callers concurrently:

```text
client thread
  -> AstraVfsClient
  -> astra_vfs_port_transport()
  -> storage receive port
  -> storage worker pool
  -> astra_vfs_port_service_worker_pump(..., worker_scratch, 1)
  -> AstraVfsService
  -> AstraVfsExt4Backend
  -> lwext4
  -> AstraExt4Port
  -> AstraBlockDevice
  -> host-backed block request lanes
```

Protocol v20 keeps one process-wide VFS session and namespace while assigning
each calling thread its own reply channel, transaction identity, transfer
area, direct-transfer scratch, and wire records. The service keys retained
reply resources by `(session, lane)` and the storage worker count is the
advertised block queue depth plus one CPU/cache lane. A first-use connection
is published once; later calls do not pass through that connection gate.

Remaining concurrency work is below lwext4's retained read split: mutation
and journal paths still serialize where their shared filesystem structures
require it, and the physical device queue depth remains the real ceiling.

The kernel already supplies the needed scheduling substrate: same-process
threads avoid a CRP/ATC switch, ports provide bounded FIFO/backpressure,
events and semaphores block without polling, and block requests and completion
IRQs are waitable. Do not add another scheduler, fiber package, request broker,
or polling loop.

## 3. Required semantics

### 3.1 Concurrency boundary

The VFS service may execute requests from different sessions and from
different thread lanes of one session concurrently. `AstraVfsClient` is
process-shared: all threads see the same namespace and open-file table, but
each synchronous thread owns an independent request/reply/bulk lane. No
session-wide transport lock may serialize those lanes.

A second request on the same lane returns `ASTRA_VFS_ERR_BUSY`; independent
lanes remain admissible. Ordering is attached to the actual shared object:
conflicting operations on one open file or inode may wait, while unrelated
files and cached work proceed.

### 3.2 Linearization

Each completed operation must appear to take effect at one point between its
request and reply.

- Explicit-offset reads return either bytes ordered before a conflicting write
  or bytes ordered after it, never a mixture caused by torn cache state.
- Explicit-offset writes publish only the count returned to the caller.
- append reserves its final offset atomically with other appends to the same
  inode.
- rename is atomic to lookup and enumeration.
- create-exclusive has one winner.
- truncate is ordered with reads and writes of the same inode.
- `sync`/`fsync` does not reply before every write ordered before its barrier is
  durable according to the existing block flush contract.
- close and session teardown cannot reuse a handle slot until every request
  that pinned the old generation has stopped using it.

Operations on the same inode may serialize. Journal commits and allocation
bitmap changes may serialize. Unrelated cached reads must not serialize merely
because they share a mount.

### 3.3 Failure behavior

- Device reset, media change, cancellation, timeout, service shutdown, and
  client death wake every affected waiter exactly once.
- A dead client drops its reply but still completes or rolls back any admitted
  filesystem transaction safely.
- No thread is killed asynchronously while it owns filesystem state.
- A worker fault terminates the protected storage process under the existing
  resident-service policy; it must not leave a damaged service running.
- Error translation remains at the existing boundaries. Concurrency code must
  not turn a device error into `NOT_FOUND`, `BUSY`, or a client timeout.

## 4. Execution model

Use the existing receive port as the work queue. Do not add a dispatcher queue.

The storage main thread and its worker threads all wait on the same receive
endpoint. The kernel port already owns message ordering, bounded capacity,
sender authentication, peer-death behavior, and backpressure. Each worker
owns its request message, reply message, temporary paths, activity value, and
backend scratch. No request/reply buffer may be function-static.

```text
                 +--> worker 0 --+
VFS receive port +--> worker 1 --+--> shared VFS state --> filesystem handler
                 +--> ... -------+
```

Refactor the current pump into a one-request operation with caller-owned
scratch, for example:

```c
uint32_t astra_vfs_port_service_run_one(
    AstraVfsPortService *service,
    AstraVfsPortWorker *worker,
    uint64_t deadline);
```

The exact name is not ABI. Preserve the existing pump as a thin host-test and
single-thread adapter if callers still need it.

Worker count is derived from the device, not chosen as a small magic number:

```text
filesystem workers = advertised block queue depth + one CPU/cache lane
```

The current physical backend has one active transfer, so its first useful pool
is two filesystem workers. A versioned block geometry field must advertise
queue depth before a later backend uses more than one. Legacy geometry means
depth one. The release resource budget must reserve these threads; silently
falling back to one worker is not a successful concurrency boot.

The receive port uses the shared `ASTRA_PORT_MESSAGES_MAX` capacity unless a
measured owner budget requires less. Do not retain the private value two or
invent an unbounded user-space queue.

The main request path must remain allocation-free after startup. Worker
records, paths, transfer lanes, synchronization objects, and backend scratch
are allocated and charged before the service reports ready. A request never
discovers halfway through a write that the service needs memory for its own
bookkeeping.

## 5. Shared VFS state

Add one short-held sleepable state lock to `AstraVfsService`. Use existing
Axiom semaphore/event primitives and add the missing runtime wrappers once;
do not implement a VFS-only scheduler or spinlock. All filesystem workers run
at the same service priority, so a binary semaphore is sufficient for the
first single-core implementation. Replace it with the planned native
priority-inheriting mutex when that shared primitive lands; do not create a
private mutex ABI inside VFS.

The state lock protects only:

- session lookup, ownership, busy/closing state, and rename staging;
- VFS handle slot reservation, generation, flags, reference count, and close
  transition;
- reply-session and mapped-area table publication/removal;
- VFS counters and high-water marks.

It must not be held across:

- a backend operation;
- block submission or wait;
- port send/receive;
- area map/unmap or handle close;
- allocation or filesystem recovery.

Use reserve/pin/commit rather than holding the state lock through work:

1. validate the authenticated owner and session;
2. reject a busy/closing session, then mark it busy;
3. validate and pin the file generation, or reserve an `OPENING` slot;
4. release the state lock;
5. execute the backend operation;
6. reacquire the state lock and publish or roll back the result;
7. clear busy, drop references, and detach any deferred close work;
8. release the lock, then perform detached closes and send the reply.

File slots need explicit `FREE`, `OPENING`, `OPEN`, and `CLOSING` states plus
an in-flight reference count. Generation advances only on the final
`CLOSING -> FREE` transition. Session teardown marks the session closing,
prevents new pins, and closes files after their references reach zero.

No backend callback may acquire the VFS state lock. This gives one lock order
and prevents backend I/O from recursing into service bookkeeping.

## 6. Transport and client state

Client wire records are per-thread. Protocol v20 retains reply handles and
transfer-area mappings per `(session, lane)`, not per process or per session.
The server wire records live in caller-owned `AstraVfsPortWorker` records, one
per storage worker. Personality exec exports and restores only the surviving
caller's lane; other thread lanes are not collapsed into it.

Transport rules remain unchanged:

- sender identity comes only from `astra_port_receive_from()`;
- a moved handle has exactly one owner on every failure path;
- transaction IDs match replies;
- a full service port returns `BUSY` and applies backpressure;
- replies are never retried to a different destination;
- activity is adopted per worker and restored before that worker receives its
  next request.

Host tests queue two application requests from two real threads sharing one
client before either reply is released. They also prove concurrent lazy first
use creates one session and distinct lanes rather than racing two sessions.
The physical Arty stress gate exposes QEMU's active-job gauge and high-water
mark. A four-worker run completed 400 operations with `inflight=0` at the end
and `max-inflight=3`; the gate now rejects a multi-worker run that never
exceeds one host job. This proves the per-thread lanes overlap on the
production boundary rather than merely admitting several guest callers.

The kernel-free raw-channel gate exercises the stronger per-thread invariant:
one channel publishes batches while earlier commands remain outstanding. Its
physical Arty qualification completed 100,000 commands in 1,586 submissions,
reached 64 simultaneous host jobs, and sustained 28,281 commands/s. The empty
doorbell control sustained 236,806 writes/s. A future transport change that
serializes a lane therefore fails the raw gate even if a multi-threaded VFS
test still happens to overlap separate lanes.

## 7. Filesystem-handler contract

Change the backend contract from “called from one thread at a time” to:

> Backend operations may be called concurrently. A backend owns the locking
> required by its filesystem. It must not rely on the VFS service lock and
> must not retain pointers into a request after returning.

Do not add command-specific or ext4-specific serialization to the VFS core.
Synthetic handlers may use immutable data or their own small locks. The ext4
handler owns ext4 concurrency.

The ext4 backend must protect:

- open-file slot reservation and reuse;
- each `ext4_file` cursor while an operation uses it;
- its reusable directory scan, or replace that single scan with worker-owned
  scan state;
- mount lifecycle and read-only/media-change transition.

The current shared directory scan may be serialized by a scan lock initially;
that limits directory enumeration only. It must not serialize unrelated file
reads.

The cached-read hot path is deliberately short:

```text
receive -> validate/pin -> cache lookup -> copy to caller area -> reply
```

It performs no dynamic allocation, journal operation, block syscall, mount
lock, intermediate whole-request copy, polling call, or cross-address-space
helper RPC. The brief VFS pin and cache lookup locks are released before
copying bytes. Consecutive protocol reads should remain fused into the existing
bulk operation rather than returning to the client per cache block.

## 8. lwext4 concurrency

### 8.1 Correctness checkpoint

First install lwext4's existing mount lock and run the complete concurrent
correctness suite. This establishes a serialized oracle and proves the worker,
transport, VFS pinning, teardown, and reply ownership changes independently.

This checkpoint is not completion: the lock spans block I/O and fails the
cache-hit-overtakes-stalled-miss gate.

### 8.2 Required interleaving

Before weakening the mount lock, audit every lwext4 object live across
`bread`, `bwrite`, and journal flush calls. Record the audit beside the code.
At minimum it covers:

- block-cache entry state, reference counts, dirty state, and eviction;
- inode and block-group references;
- `ext4_file` cursor mutation;
- directory cursor buffers;
- the mount's `curr_trans` journal pointer and commit sequence;
- free-block/free-inode counters and allocation bitmaps;
- mount/unmount/recovery and media-generation changes.

Implement the smallest lock split that passes the required proof:

- mount lifecycle lock: exclusive for mount, recovery, read-only transition,
  and unmount;
- journal transaction lock: serializes transaction creation/commit and write
  metadata that lwext4 represents through the single `curr_trans` pointer;
- cache-state lock: protects cache lookup, entry state, LRU links, and
  reference counts, but is released before device wait;
- a physical fill lane: one miss owns the current depth-one device; peers
  recheck cache state after waking and reuse its result rather than issue a
  duplicate read. A future device advertising depth above one requires
  per-buffer coalescing across its real lanes before that depth is enabled;
- inode/file lock only where operations conflict on the same object.

On this single CPU, locks protect thread interleaving, not simultaneous memory
access. They must sleep or use bounded preemption exclusion for a short
nonblocking state change; they must never spin and must never keep preemption
disabled across block I/O, allocation, or a port operation.

Do not release the existing mount lock around a block call without the audit
and lower-level locks. That is a data-corruption bug disguised as concurrency.
Do not create multiple lwext4 mounts of the same writable volume: their caches
and journals would be incoherent.

Writes may remain journal-serialized in the first release. The required gain
is that unrelated cached reads and namespace lookups can run while another
request waits for media. Relax write serialization only after measurements
show it is the next limit.

Journal serialization is not permission to hold the journal lock during
unrelated data copying or device completion waits. Prepare a transaction,
publish the exact dirty buffers it owns, release CPU-side locks, issue or join
I/O, and reacquire only to commit state. `fsync` waits for its required barrier;
ordinary writes may use the existing journal/cache policy and reply at the
point that policy defines, without forcing every write into a synchronous
flush.

Any changes inside `third_party/lwext4` require numbered Astra patches and an
update to `third_party/lwext4/ASTRA_VENDOR.md`; hidden local edits are not
acceptable.

### 8.3 Retained Phase D read split (2026-08-25)

The serialized oracle first held A in a physical read and observed warm B
still blocked after 100 ms solely because A owned the mount lock. Patch 0009
then retained the following split:

- writes, namespace mutation, journal state, mount lifecycle, directory scans,
  allocation counters, and recovery remain under the exclusive mount lock;
- `ext4_fread` takes the shared mount side;
- cache lookup, LRU links, flags, and reference counts use a short cache lock;
- a cache miss pins its buffer, releases the cache lock, then sleeps on the
  fill lane before touching the depth-one device;
- after acquiring the fill lane it rechecks `BC_UPTODATE`, so a same-block peer
  consumes the first result rather than reading twice;
- direct full-block reads use the same fill lane, matching the advertised
  physical queue depth rather than racing the port's single transfer buffer.

Live-across-I/O audit: a read holds its `ext4_file` only through its owning VFS
file slot and session, an inode reference pins its cache block, and a data miss
pins its own cache buffer. No read-fill device wait owns the cache or VFS state
lock. Read paths do not touch `curr_trans`, allocation bitmaps, free counters,
dirty lists, directory iterators, or the physical unaligned-I/O scratch buffer.
Those objects remain behind the exclusive writer/lifecycle side. Cache
eviction pins a dirty victim before releasing the cache lock for its flush.

The deterministic whole-library gate now proves all three threads at once:
A is stalled in a cold read, cached B finishes before A is released with no
device call, and a second cold A remains asleep then returns identical data.
The one 4 KiB fill is exactly the two physical calls required by the configured
four-sector transfer cap; the peer adds none. Normal, partitioned, ASan/UBSan,
TSan, and MC68030 builds pass on Beast.

The independent image checker exposed an older checksum-ordering defect while
this gate was being made real: indexed-directory initialization wrote the empty
entry inode after computing the leaf checksum. Patch 0010 moves that write
before the checksum. Raw, partitioned, full-volume, sanitizer, and fresh-remount
images now pass `e2fsck -fn`; the concurrent disjoint-writer files also survive
fresh-mount byte verification.

Patch 0011 closes the durability hole beneath `fsync` and journal commit.
lwext4's shared block interface now has an optional flush callback; Astra maps
it to the existing block `FLUSH` request. Journal records are flushed before
the commit record, the commit record is flushed before success, and cache
flush ends with a device barrier. A deterministic volatile-media backend keeps
unflushed writes out of the crash image. Three successive unclean exits recover
the committed file byte-for-byte in a fresh process and leave `e2fsck -fn`
clean. The first physical Arty abrupt-stop checkpoint also recovers the exact
file through Astra and through independent e2fsck replay; exhaustive physical
cut-point injection remains a Phase E gate.

Patch 0012 fixes the contiguous full-block read path introduced for executable
coalescing. Cold runs still use one largest device transfer, then publish every
block into the existing coherent cache; warm runs copy cached blocks and issue
no device request. Publication overlays any already-up-to-date block so a dirty
cached byte is never replaced by older media data. Runs larger than the actual
cache capacity retain the direct path instead of using a guessed cutoff.

The retained 12 KiB oracle performs a cold contiguous read and an immediate
seek/reread, verifies identical bytes, and requires zero physical I/O on the
second read. Raw, partitioned, ASan/UBSan, TSan, MC68030, and the complete
73-command QEMU gate pass. On Arty, a never-launched command read 24 sectors on
its cold launch and zero on the next. The earlier warm `cat` number is not
evidence: the file was absent and the shell measured its error path. Longer
stress also exposed retained dead VFS sessions exhausting the storage process's
smaller handle table before the session table filled. Receive-side cleanup now
runs only on the kernel's actual resource-pressure result, reaps all dead idle
sessions, and retries the queued receive. Host, sanitizer, analyzer, MC68030,
and the full QEMU gate pass; physical pressure qualification remains required.

Patch 0015 removes a non-atomic truncate-extension workaround from the ext4
backend. lwext4 now grows the inode inside the same transaction and mount write
lock that orders truncation with other inode mutations, zeroing only an existing
partial tail block while keeping whole-block gaps sparse. The operation
preserves the file position and performs no data-block allocation or repeated
64-byte writes. The same patch fixes lwext4's read-only
write/truncate checks, which previously tested `flags & O_RDONLY` even though
`O_RDONLY` is zero.

The retained mutation oracle releases paired host threads together and proves
one exclusive-create winner, disjoint atomic append reservations, untorn
conflicting writes, a legal truncate/write order, and atomic rename/unlink
visibility. The sparse extension survives a fresh mount with every hole byte
zero and leaves the image clean under independent `e2fsck`. Normal,
ASan/UBSan, TSan, and MC68030 builds pass on Beast.

The retained VFS model now performs exactly one million operations across four
independent sessions and byte models with fixed PRNG seeds. Every request is
checked against its model, and test-only hooks force a scheduler handoff at
state-lock acquisition/release, session and file reservation, close, and reply
publication. The state-lock release/acquire pair brackets backend entry, I/O
completion, and result commit; the controlled-block oracle separately forces
the before/after I/O-wait interleaving. Normal, ASan/UBSan, TSan, analyzer, and
MC68030 builds pass. The hooks compile out of target builds. Exhaustive physical
cut-point injection remains open; this checkpoint does not claim it.

## 9. Block layer

Keep `AstraBlockDevice` synchronous to each caller. Concurrency belongs behind
that facade so filesystem handlers and deterministic host backends keep the
same API.

Refactor the lease backend into shared device state plus request lanes. Each
lane owns its transfer buffer, kernel request handle, completion result, wait
state, and media generation. The number of lanes is the queue depth advertised
by versioned geometry and is also bounded by the existing kernel per-service
request/buffer/pinned-page accounting.

One completion owner drains and acknowledges the IRQ and publishes results to
the matching lane. It may be a dedicated service thread only when the backend
can have more than one request in flight; the current depth-one backend needs
no extra demultiplexer. Never let several waiters independently consume an
unidentified shared IRQ record.

Required properties:

- one buffer is never used by two transfers;
- request handle/generation identifies the completion;
- same-block cache misses coalesce above the device;
- reset completes every active lane with its real reset result;
- media change prevents new work and invalidates the mounted cache once;
- timeout/reset collection returns every pinned page and lane;
- queue saturation blocks at a waitable lane or returns defined backpressure;
  it never polls.

Do not increase the physical queue-depth claim merely because the kernel has
four request slots. The device must advertise what it can actually keep in
flight.

Submit adjacent dirty or missing sectors as the largest transfer accepted by
the device and partition boundary. Do not split at a private filesystem size
when the existing geometry already reports the real maximum. Keep every
advertised device lane occupied while eligible requests exist, but do not
issue duplicate reads for a block already loading.

## 10. Resource policy

Every finite resource is derived and accounted; none is silently unbounded.

| Resource | Source of capacity |
|---|---|
| receive queue | shared `ASTRA_PORT_MESSAGES_MAX` and port owner budget |
| filesystem workers | advertised block depth plus one CPU/cache lane |
| block lanes | device geometry, capped by kernel request/buffer/page budgets |
| request scratch | one caller-owned record per worker |
| VFS sessions/files | existing authenticated per-owner quotas and reserved service area |
| cache | existing measured lwext4 cache and allocator arena |
| synchronization objects | exact worker/lane design, charged to storage service |

If the required service threads or synchronization objects do not fit the
global release budget, resize the shared kernel tables from measured RAM and
table cost. Do not hide the problem by choosing an unrelated low VFS limit.
The kernel's ordinary-application reserve and essential-service memory floor
remain in force.

## 11. Scheduling and fairness

All storage workers begin at the same protected-service priority. A worker
blocks on the port, a filesystem lock, or an I/O completion; it never yields
in a polling loop.

The receive port supplies FIFO admission. Lock waiters use the kernel's
priority/FIFO order. A client cannot reserve a worker without first delivering
a complete validated request. Per-owner session/file/message quotas prevent
one process from consuming all storage state.

Record queue wait separately from filesystem CPU time and device wait. If
interactive requests are later delayed by proven bulk-I/O contention, use the
shared scheduler's planned RPC priority donation. Do not add a VFS-private
priority system before measurement.

Worker wakeups should hand work directly to an idle waiter through the
existing priority/FIFO port and synchronization rules. Do not wake every
worker for one request. Do not context-switch merely to move a request between
a dispatcher and worker: there is no dispatcher.

## 12. Latency and throughput policy

Optimize shared paths in this order, repeating measurement after every
retained change:

1. remove false serialization and polling;
2. eliminate redundant port/syscall/address-space crossings;
3. eliminate redundant copies and byte-at-a-time loops;
4. coalesce adjacent device transfers up to reported geometry;
5. coalesce identical cache misses;
6. keep all real device lanes busy;
7. improve cache lookup/replacement and metadata locality;
8. add bounded sequential read-ahead or delayed writeback only when a measured
   workload proves it reduces target latency or physical requests;
9. inspect generated MC68030 code and optimize the remaining measured CPU hot
   path, using assembly only for a material proven gain.

Do not add speculative read-ahead, a second cache, a write-behind daemon, or a
new batching protocol merely because conventional operating systems have one.
The existing coherent lwext4 cache, VFS bulk areas, directory batching, and
maximum-transfer geometry are used first.

For each workload, account end-to-end time as:

```text
client/transport + queue + locks + filesystem CPU + copies + device wait + reply
```

The optimization loop ends only when retained experiments cannot reduce the
non-device portion without losing correctness/maintainability, or the request
is bounded by the measured device service time or necessary MC68030 work. The
record must name that physical or instruction-level limit. “Fast enough” and
an unexplained timeout are not terminal conditions.

## 13. Observability

Extend existing VFS/block metrics rather than creating a second telemetry
path. At minimum record:

- current/peak busy workers and queued requests;
- request queue wait, lock wait, CPU time, and device-wait time;
- cache hits, misses, miss coalesces, and duplicate physical reads;
- active/peak block lanes;
- session-busy refusals and queue-full backpressure;
- cancellations, peer-dead replies dropped, resets, and media changes;
- per-lock maximum hold and wait time in qualification builds;
- worker switches within the storage address space versus cross-CRP switches.

Normal releases must not read a cycle counter on every hot-path lock after the
existing stage-8 instrumentation freeze. Counters that remain are ordinary
integer accounting or sampled by the existing metrics path.

## 14. Implementation sequence

Each step retains all earlier tests. Do not combine the lwext4 lock split with
transport refactoring in one unreviewable change.

### Phase A: baseline

1. Add a deterministic slow block backend that can hold and release a chosen
   completion.
2. Record current single-client and two-client latency, cycles, syscalls,
   switches, physical reads, service text/BSS, and allocator peak.
3. Add the stalled-miss/cached-hit test and confirm it fails for the current
   single-thread service.

### Phase B: thread-safe transport and VFS core

1. Add the missing shared runtime wrappers for thread creation, event or
   semaphore creation, signal, and reset as required by existing syscalls.
2. Move client and worker wire records out of static storage.
3. Add the VFS short-held state lock, session busy state, and file
   reserve/pin/close lifecycle.
4. Run concurrent host tests with the serialized fake backend.
5. Run ASan, UBSan, and the GCC analyzer.

### Phase C: storage workers with serialized ext4 oracle

1. Start the derived worker count on the existing receive port.
2. Install lwext4's mount lock and ext4-backend table/scan protection.
3. Prove concurrent request ownership, teardown, backpressure, and fairness.
4. Confirm the stalled-miss test still fails for the documented mount-lock
   reason; do not claim completion.

### Phase D: ext4 safe interleaving

1. [done] Complete and record the read-side live-across-I/O audit.
2. [done] Add cache/buffer synchronization in numbered patch 0009.
3. [partial] Shared file reads are live; namespace reads and directory scans
   remain exclusive until their own object audit and proof exist.
4. [done] Pass stalled-miss/cached-hit and same-block-coalescing gates.
   Concurrent disjoint writes also pass readback, remount, and `e2fsck`.
5. [done] Inspect generated MC68030 code and measure the new hot lock/cache
   path on QEMU and Arty. The retained C uses pointer increments rather than a
   per-block multiply; assembly is not justified by the measured result.

### Phase E: target qualification

1. Run the complete VFS, storage, lwext4, POSIX, Terminal, Lua, and crash
   suites on Beast.
2. Run the integrated QEMU gate with concurrent filesystem workloads.
3. Build the exact Arty image on Beast and run it on Arty without HDMI capture
   unless visual correctness is part of the failing gate.
4. [partial] Physical cold/warm block counts and command latency are measured;
   broader mixed-workload and cycle gates remain.
5. [done checkpoint] The deterministic volatile-media crash/replay gate passes
   three successive cycles with byte verification and `e2fsck`. One physical
   Arty abrupt stop also passes Astra replay, framebuffer byte verification,
   independent e2fsck replay, and a clean follow-up check. Exhaustive physical
   cut-point injection remains.
6. Update `CURRENT_STATE.md`, `STORAGE_AND_VFS.md`, the ABI record if geometry
   changes, memory/resource budgets, and the lwext4 vendor record.

## 15. Required tests

The implementation is incomplete until all of these pass:

1. stalled cache miss does not block an unrelated cached read;
2. two misses for the same block cause one physical read and identical data;
3. independent cached reads from all workers return byte-exact data;
4. concurrent disjoint writes survive unmount, remount, and `e2fsck`;
5. [done] conflicting writes, append, truncate,
   create-exclusive, rename, and unlink satisfy the linearization rules above;
   fsync durability has the separate volatile-media crash oracle;
6. close/BYE/client death racing a request causes no stale-handle use, leak,
   double close, or missing wakeup;
7. device timeout, reset, and media change wake every affected worker and
   leave zero active lanes/pinned pages;
8. receive-port and block-lane saturation apply bounded backpressure and a
   later request makes progress;
9. one abusive owner cannot exhaust another owner's sessions, files, messages,
   memory, or worker progress;
10. [done] one million deterministic randomized operations against four
    independent byte models complete under forced thread switches;
11. [host repeated, physical checkpoint done] dirty shutdown/recovery passes
    byte comparison and `e2fsck`;
12. ASan, UBSan, static analysis, MC68030 build, QEMU, and physical Arty gates
    pass with no metric wrap, drop, or monotonic resource growth.
13. under mixed cached reads, cache misses, directory walks, and journaled
    writes, no ready cached request waits behind an unrelated blocked request;
14. adjacent I/O reaches the device in maximum-sized transfers and every
    advertised lane remains occupied until the eligible queue drains;
15. cached bulk reads make one copy from coherent cache storage to the bound
    client area, with no whole-request intermediate copy.

The forced-switch host test must inject a scheduling point at every shared
state transition: after reserve, before/after backend entry, before/after I/O
wait, before commit, during close, and before reply. Random timing alone is not
evidence that the races were exercised.

## 16. Performance gates

Correctness comes first. A concurrency change may not regress the warmed
single-client control merely because more threads exist.

Retain these release gates, with exact thresholds set from the Phase A target
baseline rather than guessed here:

- warmed single-client `ls` cycles and physical-read count;
- cached small-file read median and p99;
- stalled-miss plus cached-hit latency for the cached client;
- aggregate sequential and random read/write throughput;
- syscall, same-CRP switch, cross-CRP switch, and ATC/cache-flush counts;
- service text, BSS, committed pages, stack pages, and allocator peak;
- idle CPU and syscall rate at a settled Terminal prompt.

Also retain a mixed-workload trace that proves overlap rather than inferring it
from a faster total: while one request is in device wait, another worker enters
and completes the cached path; while the eligible I/O queue is nonempty, the
number of active device lanes equals advertised depth.

Assembly is considered only after the correct shared C path passes and the
generated MC68030 code identifies a measured hot instruction sequence. Keep
the C implementation and tests as the behavioral oracle.

## 17. Rejected shortcuts

- **One worker per command:** duplicates policy and does not make the shared
  filesystem concurrent.
- **Mount-wide lock as the final result:** correct but blocks cache hits behind
  media latency.
- **Unlocking lwext4 only around `bread`/`bwrite`:** unsafe without protecting
  every object live across the wait.
- **Several writable mounts of one volume:** incoherent caches and journals.
- **Polling workers or IRQs:** wastes the only CPU and recreates races already
  solved by waitable objects.
- **Unbounded request allocation:** lets clients consume recovery memory.
- **A private VFS scheduler, mutex ABI, or priority scheme:** duplicates Axiom.
- **Per-command filesystem fast paths:** violate the shared VFS contract and
  leave every other application slow or incorrect.
- **Reducing cache or transport sizes to make locking easier:** trades away
  measured performance and capacity instead of fixing ownership.
- **Busy-waiting to avoid a context switch:** consumes the CPU that should run
  the request capable of making progress.
- **Synchronous flush after every write:** destroys throughput and latency;
  only the filesystem's durability contract or an explicit sync supplies a
  barrier.
- **Read-ahead without a target win:** spends device lanes and evicts useful
  cache entries for guessed work.

## 18. Definition of done

The filesystem is multi-threaded when the exact production stack, not only a
mock backend:

- executes requests from different sessions concurrently;
- passes the stalled-miss/cached-hit proof;
- preserves ext4 journal and namespace correctness through stress and power
  loss;
- obeys existing resource isolation and failure contracts;
- matches or improves the warmed single-client target baseline;
- keeps cached operations off unrelated wait queues and uses the full
  advertised device queue under load;
- has no redundant whole-request copy, polling wait, or service-wide hot-path
  lock left in the measured critical path;
- passes Beast, QEMU, and physical Arty qualification with recorded source and
  artifact identities.

Anything less is a useful checkpoint, but it must be named accurately as
thread-safe transport, concurrent request admission, or serialized ext4—not a
multi-threaded filesystem.
