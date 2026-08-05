# Astra observability and namespace rules

Status: metrics contract implemented; log transport and introspection handler
are specified here and not yet built

This document exists because three requirements are architectural rather than
features. Logging, introspection, and networking all fail the same way: if the
pieces underneath them are built first and the requirement is added afterwards,
the result is a bolted-on subsystem with its own parallel conventions. The
rules below constrain every piece built from here on.

## Metrics

`sw/include/astra/metrics.h` and `sw/userspace/metrics` implement the contract.

A module keeps its counters in whatever shape suits it and publishes one group:
a name, a sampler function, and a context pointer. A reader asks the sampler to
fill a bounded array of `(name, value)` pairs. No struct layout is shared
between a module and its readers.

That indirection is the whole point. The introspection filesystem, a terminal
command, a log drain, and a test harness are all readers walking the same
registry. None of them is special-cased into the modules, and a module never
changes its accounting to stay readable.

Rules:

- Publishing costs a module only the header. `astra_op_samples` and
  `astra_op_record` are inline; only the process that owns the registry links
  the library.
- The registry is fixed at 32 groups, allocation-free, and never unregisters.
  A group's storage outlives the process.
- A sampler must not allocate, must not block, and must be safe to call while
  the module it reports on is running. A sampler that claims to have written
  more than its capacity is discarded, not trusted.
- The module supplies the sampler; the *owner* registers it under an instance
  name. One process with two block devices publishes two groups.
- Durations use an injected clock and are counted in ticks. A service under
  measurement supplies a real clock; a service in production may supply none
  and pay nothing. When a cheap user-readable cycle counter exists it swaps in
  behind the same injection point, and no module changes.

`AstraOpMetrics` is the standard shape for an operation class: calls, failures,
units, ticks, maximum ticks. `units` is named per group — sectors for a block
device, bytes for a transport, entries for a directory read.

Measured cost on MC68030: the registry is 320 bytes of text and exactly 388
bytes of BSS. A sampler costs its module roughly 700 bytes, almost all of it
the static sample-name strings. That is the price of being readable and it is
paid per module, once.

## Logging

Astra does not need a new logging subsystem. `sw/kernel/trace.c` is already a
versioned ring: 64 KiB of storage, 32-byte records, 2047 entries, a magic and
ABI version, monotonic sequence numbers, and an event enum that already covers
syscall entry and exit, context switches, IRQ delivery, and allocation
failures.

The remaining work is a bounded user record type and a syscall that appends to
that ring, so a service's log lines and the kernel's own events share one
ordered stream with one set of sequence numbers. A userspace log service drains
it; the introspection filesystem exposes it. Two ring buffers with two
timestamps and two orderings is the failure this avoids.

The kernel monitor in `sw/kernel/monitor.c` stays what it is: the kernel's own
debug console over FTDI and AstraHost SPI. It is not the user terminal, in the
same way that a kernel console is not a tty.

## Introspection filesystem

Astra will expose live system state as a filesystem, in the spirit of `/proc`.

It is not a special mechanism. `docs/STORAGE_AND_VFS.md` already defines one
node-oriented handler contract with FAT/exFAT, a native writable volume, and
RAM as handlers. The introspection handler is another handler implementing the
same contract, with a synthetic tree whose leaves are rendered from the metric
registry, the trace ring, and per-process kernel state.

This imposes one requirement on the VFS handler contract, which must be
honoured when the contract is written rather than retrofitted:

- A handler must be able to serve nodes whose contents are generated at read
  time and whose size is not known before the read. Bounded, cookie-based
  enumeration and bounded-offset reads already allow this; nothing in the
  contract may assume a stable on-disk size or a stable inode-to-block mapping.

### PROC:

The first tree is process state, mounted at the `PROC:` assign. A process is a
directory named by its identifier; the leaves inside it are generated at read
time:

```text
PROC:
  42/
    status      identity, state, priorities, exit reason
    mem         resident frames, stacks and guards, handle references
    cpu         run count, timer ticks, syscalls, waits
    threads     one line per thread
```

Most of this is already tracked. `KernelProcessSnapshot` carries identity,
generation, owner, process and thread state, priorities, thread and live-thread
counts, run count, timer ticks, syscall count, fault vector and address, exit
status and reason, user and supervisor stack and guard pages, handle
references, and death waiters. `kernel_memory_owner_frames()` returns a
process's resident frame count from a maintained owner ledger in constant time.
Rendering `status`, `mem`, and `threads` therefore costs a query and no new
accounting.

`cpu` is the exception and must not pretend otherwise. Timer ticks and run
counts are schedule counts, not CPU time. Reporting occupancy in cycles
requires reading the cycle counter on every context switch, and that path
carries published budgets and automated regression gates. Until that cost is
measured against those gates, `cpu` reports what is actually counted and does
not report a cycle figure it cannot substantiate.

### Identifiers are generation-checked

`ps` and `kill` keep their familiar shape, but a numeric identifier alone must
never name a process. Axiom already generation-tags processes and is tested on
the invariant that a stale process handle cannot name a replacement process
after reuse. A control operation therefore carries the generation the caller
observed, and the kernel rejects it if the slot has been recycled.

This removes the standard Unix race where a program reads a pid, the process
exits, the number is reused, and the signal lands on an unrelated process. The
surface is unchanged; `kill 42` still works, and it cannot kill a different
process than the one `ps` showed.

### Reading is a view; controlling is a capability

A Unix `/proc` is ambient: any process can enumerate every other process
because the namespace is global. Astra is capability-based, and that difference
must not be papered over.

`PROC:` is a view rendered by an introspection service that holds the observer
authority. A process can see it because it was granted a handle to that mount,
and the rights on that mount decide how much it can see. Killing is not a write
to a control file: it is a process-control operation that requires
process-control authority, mediated by the same service.

The result keeps `ps`, `top`, and `kill` exactly as familiar as they should be,
without granting every process the ability to inspect every other one by
default.

How much history and which additional trees live beside `PROC:` remain open.
The constraint that matters now is that no special case is needed to add them.

## Networking is not a later layer

Networking is not planned as a subsystem bolted beside the filesystem. The
decision that keeps it from becoming one is made now, before the VFS client
contract is written:

- **A process-visible handle is a general object handle, not a file
  descriptor.** Lifetime, rights, duplication, transfer, and revocation are
  properties of the handle, not of the fact that it happens to name a file.
  Axiom already models handles this way for events, ports, areas, rings, IRQ
  endpoints, and device leases. The VFS client layer must not narrow that.
- **Service protocols are generic request/reply over ports and bulk rings**,
  not file-specific RPC. A network endpoint and a file both become services
  answering a bounded request record with a bounded reply and, for bulk data, a
  shared ring.
- **Network objects are nameable in the same namespace.** Astra does not adopt
  the Plan 9 position that everything must be a file, but a network endpoint
  must be reachable by name and readable through the same introspection surface
  as everything else.

No networking code exists yet, and none is warranted until storage and a
terminal work. These three rules cost nothing today and are expensive to
retrofit.

## Performance is evidence, not intent

Every retained piece reports its own numbers, and those numbers are recorded
where the piece is documented. The existing baselines follow this pattern:
block facade at 84 ns average read and 74 ns average write on the host memory
backend; allocator at 1,270 bytes of MC68030 text carrying lwext4's full
workload with zero failures against a 151,936-byte arena.

A new implementation fails its gate if it silently regresses average or tail
latency, throughput, MC68030 cycles, copies, or memory. Threshold changes
require a recorded explanation, never an updated expected number.
