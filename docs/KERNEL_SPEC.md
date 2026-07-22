# Astra kernel specification

Version 0.1 — research baseline

This document turns `docs/OS_VISION.md` into an implementable contract for the
small privileged core of Astra OS. It is intentionally incomplete. Version 0.1
defines the kernel boundary, invariants, research questions, and acceptance
gates before it freezes syscall numbers, structure layouts, or implementation
details.

Authority, from highest to lowest:

1. `docs/OS_VISION.md` defines the product and operating-system principles.
2. `SPEC.md`, `docs/MC68030_COMPLIANCE.md`, `docs/VESTA.md`, and
   `docs/SDRAM.md` define accepted platform behavior.
3. This document defines the evolving kernel contract.
4. Source code must conform to the documents or update them deliberately.

Status markers have the same meaning as in `docs/OS_VISION.md`:

- **LOCKED** — established by the project owner;
- **DIRECTION** — working design to be validated;
- **PROPOSAL** — concrete candidate selected for focused comparison;
- **OPEN** — unresolved;
- **BLOCKER** — must be resolved before the dependent milestone is accepted.

The words MUST, MUST NOT, SHOULD, SHOULD NOT, and MAY are normative only where
the surrounding item is **LOCKED** or **DIRECTION**. Proposals become normative
only after their named experiment and review.

## 1. Mission and boundary

**LOCKED:** Astra uses a purpose-built, fully preemptive, protected kernel for
one MC68030-class CPU with its built-in PMMU. The kernel supplies privileged
mechanisms; protected user processes supply restartable policy.

The kernel exists to provide:

- exception, trap, and interrupt entry;
- address spaces, physical memory, mapping, and protection;
- processes, threads, preemptive scheduling, waits, and time;
- typed capability handles and bounded IPC;
- safe interrupt, MMIO, DMA, cache, and reset mechanisms;
- resource accounting, fault containment, and observability;
- the minimum boot path needed to create the initial user process.

The kernel does not natively implement:

- filesystems, path lookup, settings, packages, or executable search;
- Wi-Fi, IP, TCP, UDP, DNS, TLS, or application protocols;
- windows, graphics policy, audio policy, or media graphs;
- a Unix user/group/root model, `fork`, signals, or a POSIX syscall surface;
- demand paging, swap, overcommit, loadable third-party kernel modules, or SMP;
- complex parsers for storage, network, media, fonts, or packages.

Filesystem, display, media, input, network-broker, settings, package, launcher,
and inspection policy belong in protected services. The ESP32 running
ESP-IDF/FreeRTOS owns SD protocol and Wi-Fi through TCP/UDP.

## 2. Kernel invariants

The first implementation and every later optimization preserve these rules:

1. Ordinary application code always executes in user mode in a process-specific
   address space.
2. User mode cannot access kernel memory, page tables, ROM control state, or
   chipset MMIO.
3. A malformed syscall, invalid user pointer, user exception, exhausted quota,
   or full queue cannot corrupt or panic the kernel.
4. Every queue, table, pool, message, wait set, and kernel allocation has a
   finite capacity and defined exhaustion result.
5. The kernel never promises memory that it cannot back.
6. No interrupt handler waits for a lock, user process, service, device
   completion, storage operation, or network peer.
7. No kernel lock is held while copying an unbounded amount of user data,
   waiting, or invoking user-controlled work.
8. Rights enter a process only through its initial handle set or explicit
   handle transfer. A numeric process ID or string name grants no authority.
9. Bulk data crosses protection boundaries through explicit shared memory;
   control messages remain bounded.
10. Device access is revocable. Process or service death cannot leave DMA or
    interrupts running indefinitely.
11. Timeouts and scheduling use monotonic time, never wall-clock time.
12. Kernel behavior is measurable: latency, queue depth, allocation failure,
    interrupt storms, faults, and recovery are visible to development tools.

## 3. Platform contract

| Property | Kernel assumption |
|---|---|
| CPU | One big-endian MC68030-compatible integer CPU; no FPU |
| Addressing | 32-bit logical and physical addresses |
| Memory | 32 MiB SDRAM plus separately decoded ROM and MMIO |
| PMMU | MC68030 TC, CRP, SRP, TT0/TT1, MMUSR, ATC, and PMMU instructions |
| Caches | Separate 256-byte instruction/data caches; write-through data path |
| Coherence | PMMU cache-inhibit is honored; current DMA maintenance conservatively invalidates all cache entries |
| Interrupts | Vesta vectored controller, 32 sources, seven 68k IPL levels |
| Time | Two programmable Vesta timers and a free-running 64-bit CPU-cycle counter |
| DMA | Multiple physical bus masters not constrained by the CPU PMMU |
| Boot | ROM POST, recovery, image load, reset-overlay removal, and versioned `BootInfo` handoff |

The kernel reads RAM size, ROM location, CPU identity/features, clock rate,
hardware build ID, and instantiated personality descriptors from the validated
boot handoff. Fixed bootstrap addresses such as `VESTA_BASE` remain platform
ABI only where firmware and hardware documents explicitly require them.

### 3.1 Hardware dependencies and closed gates

The following are not software workarounds:

- **SIMULATION CLOSED, HARDWARE PENDING K-HW-1:** A cold-ATC table walk needed
  while the CPU stacks an exception on a valid translated supervisor stack now
  passes the focused RTL reproducer without a second fault or CPU halt. The
  exact retained RTL still requires synthesis, routing, and board promotion
  before protected multitasking relies on it.
- **SIMULATION CLOSED, HARDWARE PENDING K-HW-2:** Separate SRP/CRP roots plus
  faulting `MOVES.B` user-copy operations now preserve SFC/DFC, return through
  an unmodified format-B frame, update predecrement/postincrement registers
  once, and perform one target transfer. Absolute, predecrement, postincrement,
  read, and write forms pass shared and focused RTL tests. Exact hardware
  promotion remains required.
- **BLOCKER K-HW-3:** PMMU descriptor search and hardware descriptor updates
  must be indivisible against DMA as required by the MC68030 contract.
- **BLOCKER K-HW-4:** Vesta interrupt acknowledge, edge/level clearing, timer
  expiry, and one-shot reprogramming races require simulation and hardware
  tests before the scheduler relies on them.
- **BLOCKER K-HW-5:** Untrusted service-directed DMA requires either hardware
  fences or a kernel submission path with complete address, length, wrap, and
  device-state validation.

## 4. Boot-to-kernel contract

### 4.1 Entry state

**IMPLEMENTED (ABI 0.1):** Firmware transfers control once and provides facts,
not runtime services. The canonical structure and constants are in
`sw/include/astra/boot.h`; `docs/MEMORY_MAP.md` records its fixed reservations.
The handoff contract is:

- `D0 = 0x4136384B` (`A68K`) and `A0 = 0x01FF8000`, the physical
  `AstraBootInfo` address;
- supervisor mode, master bit clear, IPL 7, PMMU disabled, and reset overlay
  disabled;
- instruction/data caches enabled with the bootstrap CACR policy; exact state
  is represented by `BootInfo.flags` and must be normalized by the kernel;
- all DMA idle and interrupts masked;
- the firmware stack remains in the published bootstrap-BRAM range only until
  the kernel installs its private stack;
- sorted RAM, firmware, early-log, kernel, and ROM-backing ranges in the
  checksummed 256-byte `BootInfo` structure.

The separately linked kernel is copied and verified at `0x02010000` before
entry. The kernel validates the independent register magic, pointer, ABI
version, size, checksum, required flags, platform identity, image bounds, log,
and memory ranges before trusting the handoff. VBR and private stack ownership
move to the kernel in the first assembly instructions. USP/MSP are not inputs
to ABI 0.1 and must be initialized before use.

### 4.2 Initialization order

The kernel brings up mechanisms in this order:

1. establish a private early stack, HDMI panic output, SDRAM early log, and
   bounded diagnostic-UART mirror;
2. validate and copy the bounded `BootInfo` facts it needs;
3. install a kernel-owned VBR and complete exception stubs;
4. reserve firmware, image, page-table, stack, framebuffer, and DMA ranges;
5. initialize the physical-page allocator and permanent kernel mappings;
6. enable and verify the selected PMMU configuration;
7. initialize clocks, interrupts, scheduler, and kernel object tables;
8. construct one user address space and initial capability set;
9. enter the initial user process;
10. enable ordinary device interrupts only after their recovery path exists.

Failure before user entry prints a bounded diagnostic and returns to recovery
by reset. Firmware calls are not made after handoff.

## 5. CPU context, exceptions, and faults

### 5.1 Saved execution state

A stopped user thread's complete architectural state includes:

- `D0-D7` and `A0-A6`;
- user stack pointer;
- PC and a sanitized user SR;
- the architecturally generated exception frame needed by `RTE`;
- kernel bookkeeping identifying why and where the thread stopped.

PMMU roots, ATC state, cache controls, VBR, SFC/DFC, and supervisor stack
registers are kernel/CPU state, not user-controlled thread registers. The
context-switch design must explicitly say which are constant, per-address
space, per-thread, or transient. No FPU state exists.

### 5.2 Exception entry

**DIRECTION:** Each vector enters a small assembly stub that:

1. prevents unsafe nested entry at the appropriate IPL;
2. saves registers without altering the CPU-created frame;
3. identifies the vector and frame format;
4. switches to C only after a valid guarded kernel stack is known;
5. dispatches to syscall, synchronous fault, interrupt, debug, or panic logic;
6. reschedules if required;
7. restores an explicitly validated context and returns with `RTE`.

Formats 0, 1, 2, 9, A, and B must be distinguished, and variable-length frames
must never be treated as a format-0 C structure. Frame decoding is centralized
and tested with byte-exact fixtures derived from the Motorola manual and real
probe output.

**OPEN:** Whether Astra uses one per-thread supervisor stack with `M=0`, or
separates master and interrupt stacks using MSP/ISP. The simplest design is not
chosen until nested interrupt, preemption, cold-ATC stacking, and context-switch
probes demonstrate the actual core behavior.

### 5.3 Fault policy

- A user address, privilege, illegal-instruction, divide, or unrecoverable MMU
  fault stops only the offending process and emits a structured fault record.
- A fault during a narrowly annotated user-copy operation returns an explicit
  bad-address status only if vector, access type, and address match that copy.
- A kernel fault outside such a recovery region violates an invariant and
  enters the panic path.
- Fault delivery to a debugger service may be added after unconditional
  containment works; no debugger is required for correctness.
- The kernel never resumes a modified exception frame supplied by user space.

### 5.4 User access helpers

`copy_from_user`, `copy_to_user`, scalar loads/stores, and string copying are
the only normal kernel paths that dereference user addresses. They:

- check range addition for overflow;
- limit total bytes;
- never hold unrelated locks;
- provide a precise fault-recovery scope;
- report partial progress only where the public ABI defines it;
- are tested across page boundaries and faults on every byte position.

The selected PMMU root strategy determines whether these helpers use ordinary
loads/stores or `MOVES` with explicit user function codes.

## 6. Virtual memory and PMMU

### 6.1 Initial VM feature set

The initial VM provides:

- one protected user address space per process;
- permanent supervisor-only kernel and MMIO access;
- anonymous committed memory;
- read-only executable and data mappings where supported;
- guarded stacks and an unmapped null region;
- explicitly created shared memory objects;
- mapping, unmapping, protection reduction, and deterministic teardown.

It does not provide demand paging, swap, copy-on-write, memory-mapped files,
automatic stack growth, memory compression, or overcommit.

### 6.2 Page-table proposal

**PROPOSAL:** Start experiments with 4 KiB pages and a 10/10/12
logical-address split. For the SRP/CRP-separated design, first use two levels
of short-format descriptors. This gives:

- 8,192 physical frames in 32 MiB;
- a 1 KiB physical-allocation bitmap;
- one 4 KiB user root table per address space in that candidate;
- 4 KiB second-level tables allocated only for populated regions;
- straightforward page alignment for ELF, guards, sharing, and DMA buffers.

MC68030 short descriptors do not contain the supervisor-only protection field.
Consequently, this exact short-table design is safe only when supervisor and
user translations are separated at the SRP/CRP or function-code level. A
unified tree must compare long-format protected descriptors or another
Motorola-defined separation, with the corresponding table-memory cost.

The proposal is not accepted until the TC value, root descriptors, table
limits, descriptor bits, protection behavior, and exact table-walk traffic are
checked against MC68030 manual sections 9.1-9.9 and the supported RTL core.

### 6.3 Root and kernel-mapping alternatives

Research must compare these rather than selecting by familiarity:

| Alternative | Benefit | Cost or dependency |
|---|---|---|
| Permanent SRP for supervisor, per-process CRP for user | Kernel tables do not change on a process switch; clean separation | User copies require reliable SFC/DFC and `MOVES`; focused and shared simulation now pass, but exact hardware promotion remains |
| Per-process unified root with protected supervisor subtree | Ordinary kernel access to mapped user memory; familiar pmap model | Requires long-format supervisor protection or function-code separation; kernel mappings appear in every root |
| Transparent-translation-assisted kernel aperture | Some supervisor mappings avoid table walks | Identity mapping only, minimum 16 MiB aperture, no descriptor protection, strict function-code matching, consumes TT0/TT1 |

The experiment records context-switch cycles, required flushes, table memory,
copy-helper complexity, exception behavior, and failure containment. It must
force ATC misses rather than accidentally passing with warm translations.

### 6.4 Mapping and ATC rules

- Page-table pages and active kernel stacks are wired and kernel-owned.
- User mappings never contain MMIO or page-table physical frames.
- Mapping changes become visible only through a documented descriptor-write,
  ordering, and `PFLUSH` sequence.
- Root changes use the Motorola-required programming order and flush scope.
- Page-table descriptor hardware updates cannot race DMA or kernel reuse.
- A physical frame is not returned to an allocator until no root, ATC entry,
  DMA engine, or shared-memory handle can reference it.
- Read-only is enforced by the PMMU. Hardware execute-disable is unavailable;
  software W^X remains policy, not a claim of NX protection.
- Cacheable and cache-inhibited aliases of one physical frame are forbidden.

## 7. Physical and kernel memory

### 7.1 Physical pages

**PROPOSAL:** Use a boot-time range allocator followed by a 4 KiB frame bitmap
with bounded contiguous-run allocation. With only 8,192 frames, a complete
bitmap scan has a small known upper bound and is easier to audit than a large
general-purpose VM allocator. Revisit a buddy allocator only if measured
fragmentation or DMA allocation requires it.

Every frame has kernel metadata sufficient to represent:

- free, kernel, page-table, process-private, shared, DMA, device-reserved, or
  firmware-reserved ownership;
- reference/pin count with checked overflow;
- cache/DMA state;
- owning resource account or explicit shared ownership.

### 7.2 Kernel allocations

Small fixed kernel objects use typed size-class caches or object pools backed
by committed pages. Interrupt and fault paths use preallocated reserves and do
not invoke a general allocator. Allocation flags must state whether failure is
allowed and whether waiting is permitted; an allocator never silently blocks
in an interrupt context.

Allocator poisoning, guard values, ownership tags, high-water marks, and
deterministic failure injection are required in development builds.

## 8. Processes, threads, and lifecycle

### 8.1 Objects

- A **process** owns one address space, one handle table, one resource account,
  and a failure boundary.
- A **thread** owns schedulable CPU state and belongs to exactly one process.
- A **resource account** limits committed memory, handles, threads, queued IPC,
  timers, shared areas, and elevated scheduling use.

Diagnostic process/thread IDs may exist but never grant authority.

### 8.2 States

**DIRECTION:** Process states are `CREATED`, `RUNNING`, `EXITING`, and `DEAD`.
Thread states are `CREATED`, `READY`, `RUNNING`, `BLOCKED`, and `DEAD`. On one
CPU exactly one thread is `RUNNING`.

State transitions occur under one documented ownership rule. A thread cannot
be destroyed while it might own process locks. Unconditional termination
destroys the process as a unit, revokes its device work, wakes peers with a
peer-death result, and then reclaims its address space and handles.

### 8.3 Creation

Native creation is explicit. The kernel creates an empty process and thread;
a protected loader/supervisor service parses ordinary ELF, maps segments,
constructs arguments, and supplies the initial capabilities. There is no
native `fork` or implicit inheritance of all parent authority.

Bootstrapping is the narrow exception: firmware supplies one immutable initial
user image and a bounded load description in `BootInfo`. The kernel validates
its physical ranges, virtual mappings, sizes, rights, and entry point without
performing filesystem lookup or general-purpose ELF policy. Once that initial
supervisor service runs, all later executable parsing and loading occurs in
user space.

The kernel validates final entry PC, stack, mapping permissions, and initial
SR before starting a thread. The loader, not the kernel, owns filesystem paths
and bundle policy.

## 9. Scheduling, synchronization, and time

### 9.1 Initial scheduler

**PROPOSAL:** Use fixed-priority FIFO ready queues plus a bitmap for bounded
ready selection, with round-robin quanta inside ordinary priorities. Begin with
a small number of priority levels and reserve ranges for:

- interrupt-deferred and critical system work;
- capability-controlled time-sensitive/media work;
- interactive work;
- normal and background work.

The first vertical slice needs only ordinary preemptive round-robin behavior.
Priority ranges, quanta, process fairness, and media budgets are not frozen
until latency and starvation tests exist.

Elevated priority is a capability and a budget, not a process preference.
Thread-count amplification must not let one process consume more ordinary CPU
share merely by creating threads.

### 9.2 Clock model

- **PROPOSAL:** Public durations and deadlines are signed 64-bit nanoseconds;
  the physical clock may have coarser resolution.
- The Vesta free-running cycle counter is the candidate monotonic source, with
  a specified rollover-safe high/low read sequence. It must first be proven to
  continue across CPU `STOP`, idle, and every non-reset power state the kernel
  uses.
- A one-shot timer programs the next quantum, timer, or wake deadline where
  practical; periodic fallback is allowed only during bring-up.
- Timer programming defines behavior for already-passed, minimum-distance, and
  maximum-distance deadlines.
- Wall-clock time is maintained by a service and never drives kernel waits.

### 9.3 Kernel synchronization

On the initial uniprocessor target:

- interrupt masking or IPL raising protects tiny IRQ-shared regions;
- preemption disabling protects tiny scheduler-local regions;
- sleepable mutexes protect longer thread-context operations;
- interrupt handlers never acquire sleepable locks;
- no code spins waiting for a running peer on the same CPU;
- priority inheritance is required for kernel mutexes that can block an
  elevated-priority thread behind a lower-priority owner;
- lock rank and maximum interrupt/preemption-disabled duration are recorded.

Cross-service priority donation is **OPEN**. Avoiding nested synchronous IPC is
the primary defense against distributed priority inversion.

## 10. Kernel objects and capability handles

### 10.1 Initial object set

The minimal object vocabulary is expected to include:

- process, thread, and resource account;
- address space, memory object, and shared area;
- channel endpoint, event, timer, and wait/notification object;
- interrupt lease, device lease, and DMA buffer;
- debug/inspection access where explicitly granted.

Service names, filesystem objects, sockets, windows, and media objects remain
user-service concepts represented to applications by service-issued handles or
protocol identifiers.

### 10.2 Handle rules

**DIRECTION:** A user-visible handle is a small integer indexing a
process-private kernel table. Zero is invalid. The table entry, not user bits,
holds the object reference, type, rights, generation, and accounting owner.

- Lookup checks index, generation, expected type, and required rights.
- Duplication can preserve or reduce rights but never add them.
- Transfer through IPC is atomic and moves authority by default.
- Closing the final reference triggers a defined object terminal state.
- Process death closes every remaining handle.
- Table capacity is charged before an IPC transfer commits.
- Stale numeric values fail after slot reuse.

Exact index/generation encoding, maximum handles, revocation trees, and whether
diagnostic object IDs are 32 or 64 bits remain **OPEN**. Full seL4-style CSpace
hierarchies are not presumed necessary for a 32 MiB personal computer.

### 10.3 Rights

Rights are object-specific subsets of a small common vocabulary such as:

- inspect, wait, signal;
- read, write, map;
- send, receive, transfer, duplicate;
- start, stop, configure, reset;
- grant or manage.

The specification for each syscall states both the accepted object type and
rights. A generic super-right that bypasses object checks is forbidden.

## 11. IPC, waits, and shared memory

### 11.1 Channels

**PROPOSAL:** The primary control IPC object is a paired, datagram-oriented
channel. Each endpoint has a bounded queue charged by message count, byte
count, and attached-handle count.

An enqueue is atomic:

- either the complete message and all transferred handles are queued, or no
  visible state changes;
- no short message send exists;
- a full queue returns an explicit would-block/backpressure result unless the
  caller deliberately selected a deadline-bearing wait;
- closing one endpoint makes peer death observable and safely discards queued
  authority according to one defined rule;
- message order is FIFO per sending endpoint.

The kernel transports bounded bytes and handles; versioned service protocols
give those bytes meaning. It does not parse filesystem, network, GUI, or media
schemas.

Maximum inline bytes, handles per message, queue capacity, register fast path,
and copy-versus-rendezvous policy are **OPEN** and must be measured on the
12.5 MHz core. Bulk payloads use shared areas rather than increasing the
inline maximum.

### 11.2 Waiting

Kernel objects expose state signals such as readable, writable, completed,
terminated, peer-closed, timer-fired, and device-lost. A thread may:

- poll one object without sleeping;
- wait on one object until a monotonic deadline;
- wait on a bounded set of objects;
- bind many sources to an event port if measurement justifies a separate port
  object.

All waits define timeout, interruption, cancellation, object close, and
spurious-wakeup behavior. The kernel rechecks state while registering a waiter
so an event cannot be lost between observation and sleep.

### 11.3 Shared areas

A shared area is backed by an explicit memory object and mapped into named
processes with rights no greater than the mapping handle permits. The kernel
does not infer ownership from mappings. Every service protocol using shared
memory defines who may mutate each buffer and the state transition that hands
ownership to another party.

## 12. Native syscall ABI

### 12.1 Transport

**DIRECTION:** Use one reserved 68k `TRAP` vector with a small, C-callable
assembly veneer. The ABI must specify:

- syscall number and argument/result registers;
- caller- and kernel-preserved registers;
- 32- and 64-bit value alignment and return convention;
- pointer range rules and maximum copied argument size;
- status-code representation independent of libc `errno`;
- restart, interruption, timeout, and cancellation behavior;
- the exact sanitized user SR accepted on return.

**PROPOSAL:** Use registers for the syscall number and a few machine-word
arguments, with a bounded pointer/size pair for larger fixed-layout arguments.
The final register assignment is selected only after compiling an ABI probe
with the pinned `m68k-elf-gcc` configuration and measuring the generated veneer.

### 12.2 Mechanism families

The initial syscall surface should be derivable from these families, not from
Unix call names:

| Family | Representative mechanisms |
|---|---|
| ABI/system | query ABI, monotonic time, yield |
| Handles | close, duplicate/restrict, inspect, wait |
| Process/thread | create, start, exit, terminate, read/write debug state |
| Memory | create memory object, map, unmap, reduce protection, share |
| IPC | create channel, send, receive, create/signal event |
| Time | create/arm/cancel timer, sleep to deadline |
| Privileged device | bind interrupt, allocate/transition DMA buffer, reset/revoke device |

This table is not a syscall-number assignment. Every candidate call must pass
the kernel admission test: it requires privilege, atomicity, protection, or a
measured latency bound that cannot safely live in a restartable service.

There is no generic `ioctl`, raw kernel pointer, compiler-native persisted
structure, path operation, socket call, or GUI/media call.

## 13. Interrupts, devices, DMA, and caches

### 13.1 Interrupt handling

For each source, the kernel records trigger mode, IPL, vector, owner, mask
state, generation, acknowledger, storm budget, and deferred-work target.

The hard handler:

1. captures minimal status and a timestamp;
2. masks or acknowledges the source in the required order;
3. records a bounded completion/event;
4. wakes a schedulable kernel thread or protected service;
5. requests rescheduling and returns.

Unknown, repeated, or unclaimed interrupts are masked and reported. An
interrupt storm cannot monopolize the CPU indefinitely. A service never owns
an unrevocable raw vector.

### 13.2 Device leases

MMIO remains supervisor-only. A protected service receives a device lease and
safe kernel operations, not register addresses. On lease loss or service death
the kernel can, in bounded order:

1. reject new submissions;
2. mask device interrupts;
3. stop or fence DMA;
4. reset the engine if needed;
5. mark in-flight completions failed with the old generation;
6. reclaim buffers and permit a replacement service to bind.

### 13.3 DMA buffer state

Every DMA buffer follows a kernel-visible state machine such as:

```text
CPU_OWNED -> PREPARING -> DEVICE_OWNED -> COMPLETING -> CPU_OWNED
                         |                              ^
                         +-------- fault/reset --------+
```

Only the owner may mutate a buffer. The transition performs the required cache
maintenance, validates physical ranges and lengths with checked arithmetic,
programs any fence, and tags work with a device generation. A stale completion
cannot make a newly reused buffer appear complete.

The current conservative whole-cache invalidation is acceptable for the first
correct operation but must be measured. More precise maintenance or hardware
snooping is an optimization, not an excuse to weaken ownership.

## 14. Accounting, backpressure, and failure

Every process is charged for at least:

- committed and pinned pages;
- page tables and kernel metadata attributable to it;
- threads, handles, timers, and wait registrations;
- queued IPC bytes/messages/handles;
- shared and DMA buffers;
- outstanding device requests;
- elevated-priority CPU budget.

Reservation happens before an operation becomes visible. Multi-object
operations either reserve everything they need or fail without partial state.
Critical kernel recovery reserves are unavailable to ordinary processes.

Resource exhaustion returns a stable status and counter; it does not trigger a
random victim kill. Repeated abuse may be policy for the service supervisor,
not an implicit kernel heuristic.

## 15. Diagnostics, crash records, and panic

Development builds provide bounded tracing for:

- exception/interrupt entry and exit;
- context switches and wake reasons;
- syscalls and status results without leaking arbitrary user payloads;
- handle create/transfer/close;
- mapping changes and PMMU faults;
- queue depth, backpressure, timeout, and cancellation;
- DMA ownership, device generation, reset, and late completion;
- allocation and quota failure.

Each record uses a monotonic timestamp and stable diagnostic object IDs. Trace
buffers are preallocated and overwrite or drop according to an explicit mode;
tracing cannot deadlock the event it observes.

A panic is reserved for corrupted kernel state or an invariant violation that
makes continuation unsafe. The panic path masks DMA/interrupts where safe,
captures CPU registers, exception frame, PMMU roots/status, current
process/thread, recent trace, and hardware build identity, then resets into
recovery. It does not attempt ordinary filesystem writes.

K0 mirrors its early console to HDMI, the retained SDRAM log, and the FPGA's
FTDI diagnostic UART. UART readiness is bounded by the CPU cycle counter so a
failed diagnostic sink cannot deadlock boot or panic handling. The UART is not
an ESP32 transport; all ESP32-to-FPGA application traffic remains AstraHost
SPI.

## 16. Implementation discipline

**DIRECTION:** Initial target code may use freestanding C plus small reviewed
68030 assembly modules. The final language mix remains **OPEN**, but every
language must support the pinned big-endian m68k soft-float ABI and auditable
exception/syscall boundaries.

- Assembly owns reset/entry, vector stubs, context switch, PMMU/cache control,
  and carefully bounded user-copy primitives where required.
- Portable state machines, allocators, handle logic, IPC bookkeeping, and
  scheduler policy are host-testable.
- C/assembly structure offsets are generated and statically checked.
- Fixed-width integers and checked size/address arithmetic are mandatory at
  public boundaries.
- No recursion, variable-length stack arrays, unbounded stack allocation, or
  hidden floating-point code exists in the kernel.
- Kernel stacks have a measured maximum and guard page.
- Host builds run warnings-as-errors, sanitizers, fuzzing, and deterministic
  failure injection where the code is portable.
- Target builds record compiler, assembler, linker, flags, and source revision.

The toolchain probe must lock CPU flags, alignment behavior, C calling
convention, 64-bit helper calls, soft-float attributes, ELF relocation support,
and debugger register numbering before the native ABI is frozen.

## 17. Acceptance gates

### K0 — architectural probes

- Byte-exact exception frames for trap, privilege, address, bus/MMU, and timer
  interrupt cases are captured and matched to Motorola-defined behavior.
- CRP/SRP/TC/TT/PFLUSH/PTEST behavior is covered with cold and warm ATC cases.
- Exception stacking through a required table walk succeeds.
- PMMU descriptor updates are atomic against the memory arbiter.
- The compiler/syscall/context ABI probe preserves every required register.

### K1 — protected entry

- ROM passes validated `BootInfo` to a separately linked kernel.
- The kernel installs VBR, allocator, mappings, and guarded stacks.
- One user program executes and returns through the selected trap ABI.
- User access to null, kernel, page-table, and MMIO addresses is denied.

### K2 — preemption and containment

- A one-shot timer preempts between at least two patterned register contexts.
- Every register, user stack, address-space root, and kernel stack survives
  repeated switching.
- Deliberate faults kill only the offending process and produce a complete
  record.
- Kernel user-copy faults return safely without hiding unrelated kernel faults.

### K3 — objects and bounded IPC

- Handle stale-use, type, right, duplicate, transfer, exhaustion, and teardown
  tests pass.
- IPC send/receive, full queue, peer close, timeout, and handle-transfer
  operations are atomic under injected allocation failure.
- Shared areas map with the requested rights and disappear safely on teardown.
- Ping-pong latency and throughput are measured, not merely declared fast.

### K4 — device recovery

- A cache-safe DMA operation completes through a protected service boundary.
- Killing the service revokes submissions, stops DMA, masks stale interrupts,
  increments the generation, and permits restart.
- Interrupted ESP storage/network operations fail explicitly without hanging
  the kernel or unrelated processes.

### K5 — responsiveness stress

- CPU, memory, IPC, storage, network, and device queues are saturated together.
- Interactive and media work meet published wakeup/jitter budgets.
- Queue limits, interrupt storms, memory exhaustion, device resets, and process
  crashes remain local and observable.
- Every timeout and missing test is a failing result.

## 18. Research work packages

These packages can begin before the RTL CPU is fully production-qualified. Target
probes may expose CPU defects; host models can progress independently.

### 18.1 Immediate research sprint

Work should begin with small, disposable research rigs rather than production
kernel code. Four tracks can proceed now while the CPU/MMU implementation is
still being qualified:

1. **Architecture notebook and frame fixtures:** turn the Motorola exception,
   PMMU, cache, and stack rules into cited tables; encode every expected
   exception-frame format as byte-exact host tests. The target frame dumper can
   be connected when the core is ready.
2. **PMMU table model:** write a host-side constructor and checker for the 4 KiB
   10/10/12 proposal. It must emit exact TC/root/descriptor values and reject
   overflow, aliasing, illegal protections, and malformed tables. The same
   cases later run against the RTL with cold and warm ATC state.
3. **Toolchain and ABI corpus:** compile and disassemble tiny freestanding C and
   assembly cases covering calls, traps, structures, 64-bit values, soft-float
   attributes, volatile register use, and context save/restore. This freezes
   facts about the toolchain before it freezes a public ABI.
4. **Bounded mechanism models:** host-test the frame allocator, handle table,
   rights reduction, atomic handle transfer, channel capacity, peer death, and
   injected allocation failure as explicit state machines. None of this
   depends on a working target scheduler.

A fifth short document-only track can draft `BootInfo` version 0 from the
existing ROM handoff and memory map. It remains provisional until the kernel
entry probe proves every register and address assumption.

The first PMMU research rig now exists in `third_party/musashi/astra`: an
independent manual-derived PMMU model plus an end-to-end Musashi adapter. Its
host and encoded-instruction suites are the executable baseline for KR-02.
Musashi remains a fast differential oracle, not an architecture authority.
The adapter now emits byte-checked 16-word format-A and 46-word format-B PMMU
fault frames and exercises both unchanged-frame restart and handler-completed
DIB/DOB cycles through `RTE`. It also replays prior multi-transfer cycles
without repeating their host-visible side effects. This closes the host-model
portion of KR-01/KR-03. Focused RTL tests now also pass cold translated-stack
exception entry and four faulted SFC/DFC `MOVES.B` forms with exact-once side
effects. The exact-source board run remains the architecture-authoritative
promotion gate.

The following questions require the RTL core or FPGA and therefore form the
hardware rendezvous. The first two are closed in RTL simulation and await
exact-source board promotion; the remaining items are still open:

- hardware repetition of exception stacking with a cold ATC and translated
  supervisor stack;
- hardware repetition of `MOVES`, SFC/DFC, fault restart, and `RTE` behavior;
- real descriptor-update ordering and DMA arbitration;
- Vesta timer/interrupt races and measured interrupt latency;
- actual context-switch, syscall, IPC, and cache-maintenance cycle costs.

The sprint is complete when each track produces executable fixtures or a
versioned binary contract. Prose conclusions without a reproducer do not close
an architectural question.

| ID | Priority | Question | Required artifact and exit condition |
|---|---:|---|---|
| KR-01 | P0 | What exact exception and supervisor-stack strategy is safe? | Manual-cited frame notebook, assembly frame dumper, cold-ATC stack-walk tests, nested IRQ tests, selected ISP/MSP policy |
| KR-02 | P0 | Which PMMU root and table geometry should Astra use? | Executable 4 KiB 10/10 prototype plus SRP/CRP, unified-root, and TT comparison; exact TC/descriptors/flush rules |
| KR-03 | P0 | Can kernel user-copy recover safely? | Ordinary-access and `MOVES` variants, fault on every boundary, shared Musashi/RTL equivalence, exact-once side effects, and exact-source hardware promotion |
| KR-04 | P0 | What is the native C, context, and trap ABI? | Compiler-generated ABI corpus, hand-written context round trip, proposed register/status convention, generated conformance tests |
| KR-05 | P1 | What timer and scheduler substrate is actually bounded? | Timer race/latency measurements, fixed-priority bitmap prototype, starvation/process-fairness tests, initial quantum proposal |
| KR-06 | P1 | What capability model is sufficient without seL4 complexity? | Host model for handle lookup/rights/generation/transfer/revoke, state-machine tests, memory-cost table |
| KR-07 | P1 | What IPC shape is fast and non-contagiously blocking? | Host-fuzzed atomic channel model; target measurements across inline sizes; queue/backpressure/wait semantics selected |
| KR-08 | P1 | What allocator is simplest and predictable in 32 MiB? | Bitmap/run allocator and typed object-pool prototype, fragmentation/failure-injection tests, metadata budget |
| KR-09 | P1 | What IRQ/DMA contract permits service restart? | Vesta IRQ probe, lease/generation model, cache/DMA state tests, hardware-fence decision |
| KR-10 | P2 | Where does executable loading live? | Minimal ELF loader service design, process-start transaction, malformed ELF corpus, initial capability manifest |
| KR-11 | P2 | What must be observable from day one? | Stable fault record v0, trace event list, panic handoff format, host decoder |

Recommended dependency order:

```text
KR-01 exception state ----+
KR-02 PMMU layout --------+--> K1 protected entry --> K2 preemption
KR-03 user copy ----------+
KR-04 compiler/trap ABI --+

KR-06 handles ----+
KR-07 IPC --------+--> K3 object substrate
KR-08 allocator --+

KR-05 timers/scheduler --> K2 and responsiveness budgets
KR-09 IRQ/DMA -----------> K4 recoverable devices
KR-10 loader ------------> first real protected services
KR-11 observability -----> every gate
```

## 19. Research method and source map

External systems are references, not inherited product models. Record for every
borrowed mechanism:

1. the problem it solves;
2. the invariant Astra needs;
3. the smallest idea or component being reused;
4. its memory and latency cost on Astra;
5. failure and cancellation behavior;
6. source license before copying code;
7. the test that would reject it.

Primary starting sources:

- [Motorola/NXP MC68030 User's Manual](https://www.nxp.com/docs/en/reference-manual/MC68030UM.pdf), especially sections 4, 6, 8, and 9. Sections 9.9 and 9.10 explicitly discuss operating-system page tables and paging.
- [NXP MC68030 product documentation index](https://www.nxp.com/products/MC68030) for split manuals, programmer reference, and errata.
- [NetBSD common m68k source](https://github.com/NetBSD/src/tree/trunk/sys/arch/m68k) and [current m68k pmap work](https://www.netbsd.org/changes/changes-12.0.html) for permissively licensed, maintained 68k exception, pmap, cache, and soft-float experience.
- [Linux m68k architecture source](https://github.com/torvalds/linux/tree/master/arch/m68k), especially `kernel/entry.S`, `kernel/traps.c`, process switching, and `mm/`, as a second implementation oracle. Copying GPL code is a licensing decision, not an incidental implementation step.
- [seL4 capability tutorial](https://docs.sel4.systems/Tutorials/capabilities.html), [IPC tutorial](https://docs.sel4.systems/Tutorials/ipc), [interrupt tutorial](https://docs.sel4.systems/Tutorials/interrupts.html), and [fault tutorial](https://docs.sel4.systems/Tutorials/fault-handlers.html) for explicit authority, capability transfer, bounded small-message paths, and fault delivery.
- [Zircon kernel concepts](https://fuchsia.dev/fuchsia-src/concepts/kernel/concepts) for process-private integer handles, rights reduction, datagram channels, handle transfer, peer-closed signals, wait-many, and user-space program loading.
- [System V ELF gABI](https://gabi.xinuos.com/) and [GNU binutils m68k attributes](https://sourceware.org/binutils/docs/as/M680x0-Attributes.html) for executable plumbing and soft-float object compatibility.

Reference implementations never override the Motorola architecture or Astra's
measured hardware behavior.

## 20. Open decisions register

1. Exact firmware entry registers and `BootInfo` binary layout.
2. ISP/MSP use and per-thread versus separate interrupt stacks.
3. Page size and TC/table geometry after KR-02.
4. SRP/CRP split, unified roots, or transparent-translation assistance.
5. User-copy implementation and fault-recovery encoding.
6. Kernel virtual layout and physical placement.
7. Syscall trap number, registers, status values, and version negotiation.
8. Scheduler priority count, quanta, process fairness, and media budgets.
9. Handle encoding, table capacity, rights vocabulary, and revocation.
10. IPC inline size, handle count, queue limits, fast path, and wait-set design.
11. Physical allocator and contiguous DMA allocation policy.
12. Exact object list admitted to the first stable kernel ABI.
13. Execute-protection hardware extension or documented lack of NX.
14. DMA fence hardware and reset contract per master.
15. Kernel implementation language mix and license.
16. Stable crash-record and recovery-firmware handoff formats.

An open item is closed only by a small decision record containing alternatives,
measurements or proof obligations, the selected contract, and its regression
tests.
