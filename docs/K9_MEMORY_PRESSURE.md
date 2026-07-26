# K9 Memory-Pressure Contract

Status: normative implementation contract for K9

K9 hardens the current bounded kernel. It does not enlarge the development
limits and it does not introduce the future general-purpose kernel heap. The
existing process, thread, synchronization, IPC, area, DMA, and block records
remain statically backed, but every live record is claimed through one typed
cache mechanism and every fallible allocation is visible through one
allocation-site ledger.

## Allocation phases

The allocation controller has exactly two phases:

| Phase | Entry | Exit | Permitted boot-only allocation |
|---|---|---|---|
| `BOOT` | successful physical-memory initialization | successful user-copy self-test and teardown | yes |
| `RUNTIME` | explicit boot retirement | reset only | no |

Boot retirement succeeds only when every boot-only site's current unit and byte
counts are zero. Retirement is one-way. A later boot-only request is rejected,
counted, and leaves all allocator state unchanged.

The current kernel has no useful temporary bump arena. K9 therefore treats the
early physical-allocation phase as the boot allocator instead of adding an
unused heap. Common SRP/CRP page tables are runtime-owned VM allocations. The
temporary user-copy self-test page is boot-owned and must be released before
retirement.

## Stable allocation-site IDs

IDs are ABI-independent diagnostic values. Existing IDs are never renumbered;
new sites are appended.

| ID | Site | Subsystem | Unit | Boot-only |
|---:|---|---|---|---|
| 1 | `BOOT_SELFTEST_PAGE` | boot | 4 KiB frame | yes |
| 2 | `VM_PAGE_TABLE` | VM | 4 KiB frame | no |
| 3 | `PROCESS_RECORD` | process | 540-byte record | no |
| 4 | `PROCESS_CODE_PAGE` | process | 4 KiB frame | no |
| 5 | `THREAD_RECORD` | thread | 180-byte record | no |
| 6 | `THREAD_STACK_PAGE` | thread | 4 KiB frame | no |
| 7 | `HANDLE_SLOT` | handle | 28-byte entry | no |
| 8 | `DETACHED_HANDLE` | handle | 28-byte record | no |
| 9 | `SYNC_OBJECT` | synchronization | 36-byte record | no |
| 10 | `PORT_OBJECT` | IPC | 64-byte record | no |
| 11 | `PORT_MESSAGE` | IPC | 324-byte record | no |
| 12 | `AREA_OBJECT` | area | 100-byte record | no |
| 13 | `AREA_PAGES` | area | 4 KiB frame | no |
| 14 | `AREA_MAPPING` | area | 24-byte record | no |
| 15 | `RING_OBJECT` | ring | 80-byte record | no |
| 16 | `DMA_OBJECT` | DMA | 36-byte record | no |
| 17 | `DMA_PAGES` | DMA | 4 KiB frame | no |
| 18 | `BLOCK_REQUEST` | block | 64-byte record | no |
| 19 | `EMERGENCY_FAULT_PAGE` | emergency | 4 KiB frame | no |
| 20 | `EMERGENCY_CLEANUP_PAGE` | emergency | 4 KiB frame | no |
| 21 | `EMERGENCY_LOG_PAGE` | emergency | 4 KiB frame | no |
| 22 | `EMERGENCY_RESERVE` | emergency | 4 KiB frame | internal |
| 23 | `MEMORY_GENERIC` | memory | 4 KiB frame | no |

`MEMORY_GENERIC` keeps the physical allocator testable without inventing a
false production owner. Production call sites use a specific ID. The internal
reserve site is established during memory-map initialization and is not an
injectable request; reserve acquisition through sites 19-21 is injectable and
can be exhausted deterministically.

## Allocation ledger

Each site owns these saturating 32-bit counters:

```c
typedef struct KernelAllocationStats {
    uint32_t attempts;
    uint32_t successes;
    uint32_t failures;
    uint32_t injected_failures;
    uint32_t releases;
    uint32_t current_units;
    uint32_t peak_units;
    uint32_t current_bytes;
    uint32_t peak_bytes;
    uint32_t last_owner;
} KernelAllocationStats;
```

Subsystem totals are computed from site records, so there is one accounting
authority. A successful claim commits units and bytes before publication. A
failed claim commits neither. Final release decrements the exact originating
site; physical frames retain an 8-bit site ID outside compact frame metadata.
Diagnostics reject counter overflow or release underflow as corruption.

Failure injection has one active one-shot selector:

- fail the Nth allocation attempt across all sites; or
- fail the Nth attempt at one stable site.

The selector counts only validated allocation requests. The injected request is
reported through the operation's normal resource-exhaustion result and changes
no object, frame, reference, mapping, pin, queue, or commit state.

## Typed object caches

`KernelObjectCache` contains a storage base, object size, capacity, allocation
bitmap, bounded next-fit hint, live/high-water counts, site ID, and health
state. The target descriptor is 26 bytes. Cache storage remains typed in its
owning module; no object is accessed through an unaligned or packed layout.

Claim transition:

```text
FREE --validated attempt--> RESERVED --module initialization--> LIVE
FREE --injected/exhausted--> FREE
RESERVED --publication failure/unwind--> FREE
```

Release is legal only after the module has removed all references, waiters,
queue links, mappings, pins, and child ownership. Generation is preserved by
the module across clear/reuse. The cache bitmap is the allocation authority;
module state remains the object-state authority. Debug validation requires the
two authorities to agree.

The current caches and exact capacities are:

| Cache | Capacity | Bitmap bytes | Final release point |
|---|---:|---:|---|
| process | 4 | 4 | dead, no handles, no death waiters |
| thread | 16 | 4 | dead, stack released, no handles/waits |
| synchronization object | 32 | 4 | closing/free, no references/waiters |
| port | 16 | 4 | closing, no endpoints/messages/waiters |
| copied port message | 32 | 4 | dequeued/discarded, detached handles moved/released |
| detached handle | 256 | 32 | imported, rolled back, or authority released |
| area | 8 | 4 | closing, frames/mappings/children/handles released |
| area mapping | 32 | 4 | VM descriptors removed and ATC/cache order complete |
| ring | 16 | 4 | both endpoints and area child reference released |
| DMA buffer | 32 | 4 | unpinned and backing frames released |
| block request | 4 | 4 | collected, rejected before publish, or revoked completion |

Cache metadata is 286 bytes and cache bitmaps are 76 bytes on MC68030. Frame
site IDs cost 8,192 bytes and the emergency-membership bitmap costs 1,024
bytes. Site records, tag aggregates, and selector state cost 1,200 bytes.
Including target alignment, the measured K9 fixed-state increase is 10,800
bytes. The build map is the accounting authority.

Embedded per-process handle entries are not moved to a second backing store.
Their existing table slot is claimed and released through site 7, preserving
generation and atomic import semantics while still participating in global
injection and accounting.

## Emergency reserve

Exactly 32 physical frames (128 KiB) are removed from ordinary free memory at
physical-memory initialization. Selection walks usable RAM from the highest
frame downward so low aligned ranges remain available to ordinary and DMA
allocations. Reserve frames are blocked, unowned, unpinned, and marked by a
separate immutable membership bitmap.

Only sites 19-21 may acquire them. Acquisition transfers one reserve frame to a
specific nonzero owner and dynamic frame state. Final `release` or
`release_owner` automatically returns that frame to the reserve rather than to
ordinary free memory. Reserve pages never satisfy process code, stacks, page
tables, areas, DMA, or generic allocation.

Reserve state is:

```text
AVAILABLE -> FAULT | CLEANUP | LOG -> AVAILABLE
```

There is no path from `AVAILABLE` to ordinary `FREE`. Exhaustion returns
`OUT_OF_MEMORY` without borrowing ordinary pages. Pinning a borrowed emergency
page is prohibited. Panic and retained early logging remain allocation-free;
the log reserve class exists for the future interactive monitor and can be
tested now without making panic depend on allocation.

## Ordering and bounded behavior

- Hard interrupt handlers perform no cache or frame allocation.
- Allocation/cache mutation is serialized by the existing single-core kernel
  entry discipline; callers that can race timer entry disable interrupts.
- No allocation path sleeps, waits, invokes synchronous IPC, or holds a
  spinlock.
- Cache search is bounded by capacity. Physical search is bounded by 8,192
  frames. Owner release remains bounded by that owner's ledger only.
- Teardown never allocates. Process/thread/IPC cleanup, panic output, and the
  retained log therefore remain operable with zero ordinary free frames.

## Acceptance

K9 is accepted only when all of the following are automated:

1. Every injectable site is reached and fails through both site-specific and
   global-Nth controls.
2. Every injected operation returns all frame, cache, handle, reference, pin,
   mapping, queue-byte, and commit counts to its captured baseline.
3. Ordinary allocation cannot consume any reserve page; all 32 reserve pages
   can be acquired, the 33rd fails, and release restores exactly 32.
4. User-fault retirement and process termination complete while ordinary free
   memory is zero; the retained log remains writable and no cleanup path tries
   to allocate.
5. Boot retirement succeeds with zero boot-live units and rejects a later
   boot-only request.
6. Cache validation, allocation-ledger validation, and existing pool
   validation all pass after exhaustion, close, peer death, and owner death.
7. Host, Musashi, full RTL, and repeated routed ULX3S boots pass. K1-K8 cycle
   ceilings and the 675,000,000-cycle 1,000-iteration gate do not regress.
