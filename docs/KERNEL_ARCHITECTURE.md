# Astra 68 kernel architecture

Status: normative design contract, revision 0.1 (2026-07-24)

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
| area | process or shared-memory object | `CREATED`, `MAPPED`, `CLOSING`, `DEAD` |
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

## Current K3 thread substrate

These are measured MC68030 implementation facts, not the final object limits:

| Structure/pool | Exact current value |
|---|---:|
| `KernelCpuContext` | 76 bytes, 4-byte aligned |
| `KernelProcess` | 446 bytes under the m68k ABI |
| `KernelThread` | 156 bytes under the m68k ABI |
| `KernelThreadWaitQueue` | 12 bytes under the m68k ABI |
| `KernelEvent` | 16 bytes under the m68k ABI |
| process slots | 4 (1,784 bytes static) |
| thread slots | 16 (2,496 bytes static) |
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

The current internal qualification path has sequence-checked atomic block,
priority/FIFO wake-one and wake-all, a signaled/closed event, bounded absolute
deadlines, and immediate higher-priority handoff on signal or timeout. Timeout,
signal, close, and process death remove a waiter from the object queue and
deadline heap exactly once. It does not yet expose runtime thread creation,
handle-backed user events/semaphores, public deadline operations, explicit
cancellation, priority inheritance/donation, or the stable pool sizes below.

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
