# Astra 68 kernel architecture

Status: normative design contract, revision 0.1 (2026-07-22)

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
| timebase | 64-bit Vesta cycle snapshot plus two 32-bit timers | retain monotonic counter; add/qualify one-shot deadline programming without drift |
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

1. Assembly installs the private stack and VBR.
2. C validates and copies the fixed 256-byte `AstraBootInfo`.
3. The frame allocator classifies every physical page.
4. VM constructs wired SRP and empty CRP trees, then enables translation.
5. Interrupts, scheduler, and object pools initialize with devices masked.
6. The immutable initial image receives a minimal initial handle set.
7. The first user thread enters through one validated context restore.

No firmware service is called after handoff.

### Exception or syscall

1. The assembly stub masks interrupts and saves `D0-D7/A0-A6`, USP, and the
   unmodified Motorola frame on a known supervisor stack.
2. Central frame decoding validates format, vector, origin, and length.
3. User-copy recovery handles only an armed matching access fault.
4. Other user faults mark the process `EXITING`; kernel faults panic.
5. The scheduler selects a validated context or enters supervisor idle.
6. Assembly rebuilds an architecturally valid frame and executes `RTE`.

### Hard interrupt

The hard top half may acknowledge fixed device state, append one preallocated
record, wake deferred work, and request rescheduling. It never allocates,
blocks, destroys an address space, polls a protocol, or performs IPC. Deferred
work runs in thread context; K1 additionally drains bounded teardown at syscall
safe points and in supervisor idle, with interrupts enabled in both cases.

## Current K1 structures

These are implementation facts, not the final object layout:

| Structure/pool | Exact K1 value |
|---|---:|
| `KernelCpuContext` | 76 bytes, 4-byte aligned |
| `KernelProcess` | 528 bytes, 4-byte aligned |
| process slots | 4 (2,112 bytes static) |
| handle slots/process | 16 |
| handle value | 24-bit generation, 8-bit one-based slot |
| DMA slots | 32 |
| block request slots | 4 |
| timer frequency | 100 Hz |
| scheduling policy | preemptive round-robin, one thread/process |

K1 combines process and thread state only to qualify CPU/PMMU containment.
The stable implementation splits those objects before IPC and wait APIs freeze.

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

## Required companions

- [MEMORY_MAP_AND_PMMU.md](MEMORY_MAP_AND_PMMU.md)
- [ABI.md](ABI.md)
- [LOCKING_AND_PREEMPTION.md](LOCKING_AND_PREEMPTION.md)
- [RESOURCE_OWNERSHIP_AND_FAILURES.md](RESOURCE_OWNERSHIP_AND_FAILURES.md)
- [MEMORY_BUDGET.md](MEMORY_BUDGET.md)
- [TEST_AND_FAULT_INJECTION_PLAN.md](TEST_AND_FAULT_INJECTION_PLAN.md)
- [STATUS.md](STATUS.md)
