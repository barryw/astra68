# Axiom kernel architecture for Astra 68

Status: normative design contract, revision 0.3 (2026-07-25)

This document defines the kernel shape. Exact memory, ABI, locking, ownership,
budget, testing, and implementation status live in the companion documents
linked below. `KERNEL_SPEC.md` remains design rationale; where it differs from
this contract, this contract and `STATUS.md` win.

Status words are precise:

- **LOCKED**: architecture decision; changing it requires document review.
- **CURRENT**: implemented and covered by at least a host or target test.
- **PLANNED**: selected design, not yet implemented.
- **MISSING**: required before the named release gate.

## Machine contract

- **LOCKED:** one big-endian MC68030-compatible CPU with integrated PMMU.
- **LOCKED:** 32 MiB physical SDRAM, no kernel floating point, `-msoft-float`.
- **LOCKED:** no SMP, RCU, lock-free framework, swap, or memory overcommit.
- **LOCKED:** bounded latency and predictable memory use outrank feature count.
- **LOCKED:** Motorola MC68030 behavior is the architectural authority.
- **LOCKED:** the stable VM targets 8 KiB pages by default and retains a 4 KiB
  build option for measurement and compatibility. The current 4 KiB K1 image
  remains the reference oracle until the 8 KiB build passes the same gates.

The primary architectural authority is Motorola's
[MC68030 User's Manual, revision 3](https://www.nxp.com/docs/en/reference-manual/MC68030UM.pdf).
In particular, section 9.2.2 defines reset/ATC behavior, sections 9.7.2 and
9.7.3 define page geometry and transparent translation, sections 3.5.4 and
7.6 define `NOP` pipeline/bus synchronization, and section 7.5.4 defines the
double-bus-fault halt condition. Tests and implementation notes should cite
those sections instead of relying on behavior inferred from another system.

## Privileged boundary

Only these mechanisms belong in supervisor mode:

1. exception, trap, interrupt, and `RTE` entry/exit;
2. threads, scheduling, timers, waits, and synchronization;
3. physical pages, PMMU trees, address spaces, areas, and safe user copies;
4. typed handles, rights, object lifetime, bounded IPC, and wait-multiple;
5. IRQ allocation, acknowledgement, masking, and event delivery;
6. minimal MMIO/device mappings and validated DMA ownership;
7. panic, retained logging, tracing, inspection, and debug control.

Filesystems, executable policy, networking, graphics policy, window management,
input policy, audio mixing, and ordinary device policy run in protected user
services. Display hardware is owned by one graphics service. SD and network
protocols remain on the ESP; FPGA/ESP runtime transport is SPI, never UART.
There is no stable in-kernel module ABI.

Service boundaries isolate failures, but hot paths batch work through shared
areas and bounded rings. Astra does not turn every line, input event, or block
fragment into a synchronous RPC merely to make the kernel boundary smaller.

## Driver extension policy

The public extension surface is a versioned user-space driver/service ABI, not
a loadable binary kernel extension ABI. A driver service receives only explicit
IRQ endpoints, cache-policy-checked MMIO mappings, bounded DMA buffers, and
service handles. It can be restarted and its mappings, IRQs, pins, and pending
operations can be revoked without trusting driver code in supervisor mode.

Code that cannot be isolated because it participates in exception entry,
PMMU/cache maintenance, interrupt acknowledgement, or an unavoidable
latency-critical bus transaction is source-integrated into Axiom. Such code is
not runtime-loadable: it is reviewed, rebuilt, linked, and qualified with the
kernel, may use only the internal device-boundary interfaces, and has no ABI
compatibility guarantee. Third-party binary kexts are deliberately unsupported.

The MC68030 PMMU constrains CPU accesses, not independent FPGA bus masters.
User-space DMA therefore requires FPGA-enforced address/length bounds or
kernel-submitted descriptors from pages pinned and charged to the driver
service. Until that enforcement is qualified, a user driver receives no raw
DMA programming authority. Device removal, service death, timeout, and reset
must revoke DMA before frames can be reused.

## Kernel layers

Dependencies point down only:

| Layer | Owns | May depend on |
|---|---|---|
| `arch/m68k` | vector stubs, context restore, PMMU/cache primitives, user-copy recovery | no policy layer |
| core | scheduler, waits, timers, synchronization, panic/trace | `arch/m68k` |
| VM | frame allocator, address spaces, areas, mappings, commit accounting | core, `arch/m68k` |
| objects | process/team, thread, handle table, ports, events, IRQ endpoints | core, VM |
| device boundary | IRQ top halves, MMIO mappings, DMA validation/cancellation | core, VM, objects |
| user services | all policy and complex protocol parsing | stable syscall/service ABI |

Drivers and services never edit PMMU descriptors or dereference ad hoc MMIO
structures. Object destructors do not run while a handle-table lock is held.

## MMIO contract

The architecture layer will expose only these scalar primitives to kernel
device code:

```c
uint8_t  kernel_mmio_read8(uint32_t address);
uint16_t kernel_mmio_read16(uint32_t address);
uint32_t kernel_mmio_read32(uint32_t address);
void kernel_mmio_write8(uint32_t address, uint8_t value);
void kernel_mmio_write16(uint32_t address, uint16_t value);
void kernel_mmio_write32(uint32_t address, uint32_t value);
void kernel_mmio_cpu_sync(void);
uint32_t kernel_mmio_fence32(uint32_t fence_address);
```

They perform aligned, volatile, native-big-endian accesses with compiler memory
ordering; debug builds reject width/alignment violations. On MC68030,
`kernel_mmio_cpu_sync` emits the documented post-write `NOP` that waits for the
CPU-side external write to complete. It does not prove that a posted FPGA bridge
or asynchronous engine completed; that requires `kernel_mmio_fence32` to read a
device-documented fence/status register or a sequence fence to retire. Device
wrappers own that choice. Raw volatile register-structure dereferences are
transitional K1 code and cannot become the stable driver interface.

## Hardware reliability contract

The OS-facing machine must provide the following bounded facilities:

| Facility | Current state | Stable requirement |
|---|---|---|
| timebase | CURRENT HW: 64-bit Vesta cycle snapshot plus two 32-bit one-shot timers | retain monotonic counter and qualify rollover/late-programming behavior |
| watchdog | MISSING | independent timeout, reset reason, bounded service/reboot policy |
| bus timeout | PARTIAL | every target times out to BERR and latches target/address/cycle class |
| interrupt controller | CURRENT | 32 pending/mask/config sources with stable vectored IACK |
| graphics IRQs | CURRENT SIM | vblank, raster, blit, draw, and fence completion remain bounded |
| engine recovery | MISSING | per-engine timeout/reset without resetting CPU or unrelated engines |
| early console | CURRENT | FTDI UART works before PMMU/scheduler; ESP runtime remains SPI-only |
| retained trace | PARTIAL | reserved SDRAM log exists; add allocation-free trace storage outside ordinary heap corruption |
| memory bursts | PARTIAL | current caches/line fill are retained; benchmark and qualify Motorola-compatible burst behavior |

None of these facilities may monopolize the CPU or SDRAM bus indefinitely.

## Fundamental objects

The first stable object vocabulary is deliberately small:

| Object | Ownership | Required state |
|---|---|---|
| team/process | parent resource account | `CREATED`, `RUNNING`, `EXITING`, `DEAD` |
| thread | exactly one process | `CREATED`, `READY`, `RUNNING`, `BLOCKED`, `DEAD` |
| area | creating process plus explicit mapping/child references | `CREATED`, `MAPPED`, `CLOSING` (terminal) |
| port | process/service | `OPEN`, `PEER_CLOSED`, `CLOSING`, `DEAD` |
| semaphore/event | process/service | `UNSIGNALED`, `SIGNALED`, `CLOSING`, `DEAD` |
| timer | process/thread | `IDLE`, `ARMED`, `FIRED`, `CANCELLED`, `DEAD` |
| IRQ endpoint | privileged service | `MASKED`, `ARMED`, `PENDING`, `REVOKING` |
| device mapping | privileged service | `CREATED`, `MAPPED`, `REVOKING`, `DEAD` |

Numeric process and thread IDs are diagnostic labels only. Authority always
comes from a process-private generation handle carrying explicit rights.

## Control paths

### Boot

1. Assembly installs the 8 KiB interrupt stack (ISP) and VBR.
2. C validates and copies the fixed 256-byte `AstraBootInfo`.
3. The frame allocator classifies every physical page.
4. VM constructs wired SRP and empty CRP trees, then enables translation.
5. The guarded 8 KiB master stack (MSP), deferred worker, interrupts,
   scheduler, and object pools initialize with devices masked.
6. The immutable initial image receives a minimal initial handle set.
7. The first user thread enters through one validated context restore.

No firmware service is called after handoff.

### Exception or syscall

1. The assembly stub masks interrupts and saves `D0-D7/A0-A6`, USP, and the
   unmodified Motorola frame on a known supervisor stack.
2. Central frame decoding validates format, vector, origin, and length.
3. User-copy recovery handles only an armed matching access fault.
4. Other user faults mark the process `EXITING`; kernel faults panic.
5. The scheduler returns one of three tagged results: resume the interrupted
   frame, enter the fixed deferred worker, or restore an aligned user context.
6. Assembly rebuilds an architecturally valid frame and executes `RTE`.

### Hard interrupt

The hard top half may acknowledge fixed device state, append one preallocated
record, wake deferred work, and request rescheduling. It never allocates,
blocks, destroys an address space, polls a protocol, or performs IPC. K1's
fixed deferred worker runs in supervisor master mode on a dedicated guarded
MSP. It snapshots a bounded work bitmap with interrupts masked, enables
interrupts around process reclamation, and either returns to a validated user
context or atomically sleeps in master mode. A deferred pinned-DMA reap is
retried on a later timer interrupt; syscall and idle paths no longer perform
resource destruction.

## Current K6 thread substrate

These are measured MC68030 implementation facts, not the final object limits:

| Structure/pool | Exact current value |
|---|---:|
| `KernelCpuContext` | 76 bytes, 4-byte aligned |
| `KernelProcess` | 472 bytes under the m68k ABI |
| `KernelThread` | 172 bytes under the m68k ABI |
| `KernelThreadWaitQueue` | 12 bytes under the m68k ABI |
| synchronization object | 36 bytes under the m68k ABI |
| process slots | 4 (1,888 bytes static) |
| thread slots | 16 (2,752 bytes static) |
| handle slots/process | 16 |
| handle value | 24-bit generation, 8-bit one-based slot |
| ready queues | 32 FIFO queues, two 16-bit links/thread, 32-bit bitmap |
| thread priority | process default 16, user ceiling 23, effective priority/thread |
| user stack/thread | one mapped 4 KiB page plus one unmapped 4 KiB guard interval |
| supervisor stack/thread | guarded 8 KiB stack in one 12 KiB static slot |
| supervisor stack arena | 16 slots, 192 KiB total; 128 KiB usable stacks |
| DMA slots | 32 |
| block request slots | 4 |
| ordinary quantum | 5 ms / 62,500 cycles at 12.5 MHz |
| timer programming | one-shot minimum of active quantum and earliest wait deadline |
| deadline queue | 16-entry absolute-cycle min-heap, one entry/thread, deterministic slot tie-break |
| wait registrations | 256 fixed eight-byte records, 16 reserved per thread slot |
| waitable timers | share the 32-slot synchronization pool; fixed 32-entry deadline min-heap |
| scheduling policy | highest effective priority; FIFO round-robin among equals |
| interrupt stack | 8 KiB ISP plus 4 KiB unmapped guard |
| deferred-worker stack | 8 KiB MSP plus 4 KiB unmapped guard |
| deferred work | one-bit process-reap bitmap; bounded timer retry |

The K1 worker state machine is exact: `UNINITIALIZED -> BLOCKED` at boot;
signal changes `BLOCKED -> READY`; selection changes `READY -> RUNNING`;
successful service changes `RUNNING -> BLOCKED` before user return or `STOP`.
`DEFERRED` records one retry bit and the next timer changes
`BLOCKED -> READY`. A signal arriving while service runs sets the pending bit,
so the worker performs another pass before it may block. No transition
allocates metadata or grows a queue.

The process owns its address space, resource owner, handle table, default and
ceiling policy, and aggregate lifecycle. Each thread independently owns its
saved CPU context, generation ID, process-private handle, stack mapping,
guarded supervisor stack, priority, counters, ready/wait-queue links, and
state. Process death removes all of its threads from ready and wait queues
before deferred handle/address-space/frame destruction; thread records are not
reusable until that destruction completes.

Current K8 retains K6's sequence-checked atomic block, priority/FIFO wake-one
and wake-all, handle-backed auto/manual events and semaphores, signed absolute
monotonic-nanosecond deadlines, explicit cancellation, and immediate
higher-priority handoff on signal or timeout. Timeout, signal, cancellation,
close, and process death remove a waiter from the object queue and deadline
heap exactly once. Runtime thread creation, caller-only exit, level-triggered
thread/process-death waits, one-shot waitable timers, bounded wait-multiple,
K7 ports, and K8 areas/rings are exposed through provisional ABI
`0x00010004`. Priority inheritance, RPC donation, and stable production pool
sizes are not yet exposed.

## K5 thread-lifecycle contract

K5 promotes the existing internal thread constructor and retirement machinery
without creating a second scheduler object. The exact per-thread state is:

```text
FREE -> CREATED -> READY <-> RUNNING <-> BLOCKED -> DEAD -> FREE
```

`CREATED` is private and cannot be found through a handle until every resource
has been reserved. Creation has an interruptible preparation phase for slot,
stack, frame, mapping, handle, and context setup, followed by a no-allocation
commit with interrupts masked. That commit publishes the generation-safe
handle, enqueues the thread, and applies process/scheduler counts and quota
charges as one transition. Failure before commit unwinds in reverse order and
leaves no handle, mapping, frame, stack bit, ready entry, or quota charge. A
supervisor timer may expire another waiter immediately before commit; the
boundary regression proves its ready-queue insertion and the new thread's
insertion both remain reachable and queue-valid.

`THREAD_EXIT` records one 32-bit status, removes only the calling thread from
scheduling, advances its death-queue sequence, and wakes all death waiters in
priority/FIFO order. The fixed deferred worker then unmaps the 4 KiB user stack
with interrupts enabled. A dead thread with an open handle remains a bounded
zombie: it retains its thread record and guarded 8 KiB supervisor stack so its
terminal status remains observable, but it retains no user stack frame. Final
handle close releases the record and supervisor-stack slot through the same
worker. A handle closed before death releases only its reference; it never
kills the execution reference.

The current qualification limits are exact: 16 thread records globally, 15
thread objects per process, one handle entry per published thread, one 4 KiB
user stack and one virtual guard per live thread, and one fixed 8 KiB
supervisor stack plus 4 KiB guard per retained thread object. Dead objects count
against the thread-object and supervisor-stack quotas until their final handle
is closed. These limits are intentionally smaller than the eventual stable
pool targets and are reported as implementation limits by `STATUS.md`.

There is no asynchronous thread kill in K5. Process exit and user fault remain
the only operations that forcibly retire another thread. If the calling thread
is the last live thread, `THREAD_EXIT` promotes the process to `EXITING` and
uses the existing whole-process teardown rather than a special last-thread
path.

## K6 bounded wait-set contract

K6 adds `WAIT_MULTIPLE` without adding another scheduler, timeout queue, helper
thread, or allocation path. ABI 0.2 accepts 1 through 16 handles, matching the
current process handle-table capacity. This is the exact enforced limit. Any
future increase requires an ABI revision, a measured memory charge, and the
same all-backend qualification; callers cannot assume 64 today.

The syscall copies one naturally aligned array of 32-bit process-local handles
before blocking and never retains the user pointer. It resolves and rights-
checks the complete array before consuming readiness or linking anything. An
invalid handle, unsupported object type, missing wait right, self-thread wait,
bad pointer, or oversized set therefore leaves every object unchanged.

Immediate readiness is tested in input order. The first ready or terminal
object wins, so simultaneously observable readiness returns the lowest input
index. Auto-reset events and semaphores consume exactly one unit only for the
winner; later ready members remain untouched. A dead thread also returns its
32-bit exit status as the member detail.

If no member is ready, the calling thread uses one row in a fixed
`16 threads x 16 members` registration array. Every member contributes one
eight-byte intrusive registration to its object's existing priority/FIFO wait
queue. The entire set shares one thread state and, when finite, one existing
deadline-heap entry. Publication validates all queue sequences and aggregate
queue capacities before changing `RUNNING -> BLOCKED`; a failed publication
withdraws every linked registration before restoring `RUNNING`.

Signal, target death, object close, owner death, deadline expiry, explicit
cancellation, and process retirement compete through one completion path. The
winner removes the deadline and every registration before making the thread
ready. Object-originated completion returns that member's index; timeout,
cancellation, and caller retirement return the reserved no-member value.
Duplicate handles are valid and consume distinct bounded registrations; input
order makes the lowest duplicate deterministic. `WAIT_ONE` is implemented by
the same machinery with one member and preserves its existing result layout.

The waitable types are auto/manual events, semaphores, one-shot timers, thread
death, and process death. A process handle is generation-safe authority; a PID
is diagnostic only. Waiting on self is rejected. Thread and normal process
death preserve the complete 32-bit exit detail and remain ready while a handle
is open. Abnormal process death reports its terminal result with zero detail.

Timers share the existing 32-object pool and creator quota. A parallel fixed
32-entry binary min-heap stores absolute timer deadlines with slot-number
tie-breaking. Set replaces an existing arm and clears fired state; expiry is
level-triggered and wakes every registered waiter; cancel withdraws the heap
entry, clears readiness, and wakes waiters with `CANCELLED`. Set, cancel,
expiry, close, and owner death allocate nothing and compete through the same
wait-set completion owner.

## K4 synchronization-object contract

K4 replaces the singleton qualification event with one fixed pool shared by
events and semaphores. The implementation limits are deliberately explicit:

| Resource | K4 limit |
|---|---:|
| sync objects | 32 system-wide |
| objects created by one process | 8 |
| waiters on one object | 16 |
| references on one object | 65,535 (also bounded by handle capacity) |
| event retained signals | 0 or 1 |
| semaphore count | 0 through 2,147,483,647, bounded by its creation maximum |

Each 36-byte slot owns a wait queue, nonzero generation, creator process ID,
reference count, object subtype, state, current/maximum count, and terminal
close result. Its state machine is `FREE -> LIVE -> CLOSING -> FREE`; generation
advances before reuse. Creation reserves a slot and quota before publishing a
handle. Failed handle publication closes and returns the unpublished slot.

The existing per-process handle table remains the only public lookup path.
Events and semaphores install as typed synchronization handles with explicit
query, signal, wait, and administer rights. The existing scheduler wait queue
and deadline heap remain the sole owners of blocking and timeout linkage. K4
adds cancellation to that owner; it does not add a second timeout or wake queue.

The final handle release closes a live object and wakes all remaining waiters
with `CLOSED`. Creator-process death first retires that process's own waits,
then transitions every object it created to `CLOSING` and wakes foreign waiters
with `PEER_DEAD`; deferred handle release only drops references afterward.
Single-core supervisor dispatch serializes signal, timeout, cancellation,
close, and death, and each path removes both queue links before readiness.

## Stable-kernel target limits

These limits are the initial sizing contract and must be charged in
`MEMORY_BUDGET.md` before implementation:

| Resource | System limit | Per-process default |
|---|---:|---:|
| processes | 32 | 1 |
| threads | 128 | 16 |
| handles | 8,192 entries | 256 |
| areas | 512 | 64 |
| ports | 256 | 32 |
| pending message headers | 1,024 | 256 |
| copied bytes in one message | 256 | 256 |
| queued bytes | 512 KiB | 256 KiB |
| timers | 256 | 64 |
| events and semaphores | 256 | 64 |
| wait-multiple members | 64/call | 64/call |
| handle transfers | 8/message | 8/message |
| pinned pages | 2,048 total | 256 without elevated right |

Exhaustion returns an explicit bounded error. System services may receive a
different quota through a resource-account handle; they do not bypass limits.

## Development discipline

Before adding a function, type, module, state machine, or test helper, search
the kernel and its tests for an existing owner of that responsibility. Extend
or consolidate that owner when its contract can support the requirement. A new
mechanism must have a concrete reason the existing one cannot be adapted.
Duplicated policy, state transitions, generation rollover, byte primitives,
queueing, and accounting are defects and are removed while the affected
subsystem is in development.

Every scheduler, exception, syscall, VM, IPC, synchronization, and user-copy
change starts from a target-representative cycle and image-size baseline. The
change must retain an automated budget and report the before/after result.
Generated MC68030 code is inspected before replacing C with assembly. Assembly
is preferred when measurement proves a material hot-path improvement, while a
tested C-level contract remains the behavioral oracle. Astra does not carry
speculative portability layers for another ISA.

## Required companions

- [MEMORY_MAP_AND_PMMU.md](MEMORY_MAP_AND_PMMU.md)
- [ABI.md](ABI.md)
- [LOCKING_AND_PREEMPTION.md](LOCKING_AND_PREEMPTION.md)
- [RESOURCE_OWNERSHIP_AND_FAILURES.md](RESOURCE_OWNERSHIP_AND_FAILURES.md)
- [MEMORY_BUDGET.md](MEMORY_BUDGET.md)
- [TEST_AND_FAULT_INJECTION_PLAN.md](TEST_AND_FAULT_INJECTION_PLAN.md)
- [STATUS.md](STATUS.md)
