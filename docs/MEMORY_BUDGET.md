# Axiom kernel memory budget

Status: dual 32/128 MiB kernel profile implemented and hardware-gated (2026-08-09);
historical K9/K10 measurements retained below

The physical ULX3S machine has exactly 32 MiB of SDRAM. The active Arty-hosted
machine has 128 MiB of guest RAM. One kernel image supports both exact profiles;
its per-frame tables are sized for 32,768 pages, while initialization and
allocator accounting use the RAM size reported by BootInfo. Every static pool,
frame, mapping, queue, pin, and graphics reservation is reported separately. A
budget is not permission to allocate dynamically without a quota.

The Arty's 512 MiB DDR is divided at runtime into 128 MiB of physically reserved
graphics memory, 128 MiB preallocated as cached QEMU guest RAM, and a 256 MiB
Linux/host-services budget. Guest RAM deliberately stays in Linux's normal
cached allocator; an uncached `/dev/mem` reservation would make every emulated
68030 memory access a device-memory access.

The current release kernel is 124,908 bytes on disk. Its 128 MiB frame metadata
ends at `0x02140280`, inside the `0x02044000..0x02153fff` kernel reservation,
with the retained trace address unchanged at `0x020c4000`.

The retained Arty run reports 32,768 guest pages, 32,370 initially free,
147,892 KiB QEMU RSS, 203,232 KiB Linux `MemAvailable`, and zero swap after the
terminal reaches `WORK:>`.

The filesystem cache is 1,024 x 4 KiB and lives inside a fixed 5 MiB arena in
each image that mounts ext4. The supervisor uses its arena only for bootstrap;
the protected storage service owns the long-lived cache. Process image
admission is capped at 2,048 pages (8 MiB), raised from 128 pages because the
old 512 KiB ceiling rejected the bounded arena before its allocator could run.
Pages remain allocated and charged on demand; the ceiling reserves nothing.

A shared area may contain at most 2 MiB, enough for one 1280x720 RGB565 surface
(1,843,200 bytes). The current desktop uses a 900x500 client surface (900,000
bytes); the display service separately owns one 1,843,200-byte contiguous DMA
scanout. These are charged allocations inside the 128 MiB guest, not static
reservations taken from Linux or the 128 MiB physical graphics arena.

## Physical baseline

| Reservation | Bytes |
|---|---:|
| early retained log | 16,384 |
| kernel image, trace, metadata, and stacks | 1,114,112 |
| ROM backing | 262,144 |
| maximum allocator input before the conditional USB DMA carveout | 32,161,792 |

The detailed split and bootstrap BRAM are in `MEMORY_MAP.md`. The kernel starts
from BootInfo ranges, not the arithmetic above.

## Provisional device-lease substrate (2026-08-04)

The MC68030 object reports 494 bytes of fixed BSS and 1,830 bytes of text for
`device.o`. BSS is exact: eight 16-byte lease records (128 bytes), eight
36-byte device records (288 bytes), 44 bytes of counters, 26 bytes of object-
cache state, a 4-byte bitmap, and four bytes of flags/padding. It allocates no
heap memory or physical pages. These remain candidate figures until the next
complete Axiom release is frozen and hardware-qualified.

## Measured K1 image

The 2026-07-22 exact Beast build from
`66d6094f9339469313fefb70b259d07a7c2272ce` reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 21,860 |
| `.data` | 4 |
| `.bss` | 5,128 |
| `.noinit` | 68,736 |
| supervisor stack payload | 8,192 |
| guard plus stack section including alignment | 12,816 |
| total loaded/NOLOAD content including alignment | 109,648 |
| flat kernel binary | 23,912 |

The 512 KiB kernel reservation therefore has more than 400 KiB headroom, but
unused reservation is not general free memory until the boot ABI deliberately
changes.

The exact lifecycle-soak build from
`470bf123cf24bbadf3525f91307e3d9aebe92006` is 24,876 kernel bytes and
42,008 ELF bytes. The additional 964 flat-binary bytes are qualification
instrumentation, periodic accounting, and relaunch control; they do not change
the 512 KiB reservation or the stable object-pool budget.

Exact deferred-reclamation source
`bbb1616a1e65ef56619bffb11cb21e9ea1bc5202` adds bounded per-owner frame
tracking and fault-latency instrumentation. Its normal kernel is 24,856 bytes;
the soak kernel is 25,856 bytes. The normal ELF reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 22,804 |
| `.data` | 4 |
| `.bss` | 5,160 |
| `.noinit` | 102,016 |
| `.kernel_stack`, including alignment and guard | 15,424 |
| total loaded/NOLOAD content including alignment | 146,512 |
| flat kernel binary | 24,856 |

The `.noinit` increase is exactly 33,280 bytes: two 16-bit links for each of
8,192 frames plus 64 eight-byte owner ledgers. The external eight-byte
`KernelFrameInfo` layout is unchanged. `_kernel_memory_end` is `0x02034000`,
well inside the fixed 512 KiB kernel reservation.

Exact guarded-worker source
`42f4bb55ebd5ac47d057162322e293e4999a2661`, built with committed source
identity `e108a3711befa08a309f068939dff226a21c869c`, adds a dedicated MSP
stack, guard descriptor, bounded work/retry state, and panic diagnostics. Its
normal Beast build reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 24,996 |
| `.data` | 4 |
| `.bss` | 5,240 |
| `.noinit` | 102,016 |
| interrupt-stack section including alignment and guard | 13,152 |
| worker MSP section including guard | 12,288 |
| total through `_kernel_memory_end` | 159,744 |
| flat kernel binary | 27,048 |

The worker adds one fixed 8 KiB mapped stack and one 4 KiB unmapped guard;
`_kernel_memory_end` advances to `0x02037000`, still inside the 512 KiB kernel
reservation. No queue or retry path allocates memory dynamically.

The 2026-07-24 K2 blocking/thread development build, based on
`be27074ff67005095c5ec4e6b516a5c0708df049-dirty`, reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 34,912 |
| `.data` | 0 |
| `.bss` | 7,968 |
| `.noinit` | 102,016 |
| interrupt-stack section including alignment and guard | 12,800 |
| worker MSP section including guard | 12,288 |
| 16 guarded thread supervisor-stack slots | 196,608 |
| total through `_kernel_memory_end` | 364,544 |
| flat kernel binary | 36,960 |

The image occupies 364,544 bytes of the fixed 512 KiB kernel reservation and
leaves 159,744 bytes of headroom. Its measured scheduler state is 16 x
156-byte thread records, 128 bytes of ready-queue heads/tails, an 8-byte
bitmap/count pair, and 40 bytes of pool accounting: 2,672 bytes total. The four
process records occupy 1,784 bytes. The per-thread stack arena is 16 fixed
12 KiB slots: one unmapped 4 KiB guard followed by one mapped 8 KiB supervisor
stack. Wait queues are 12 bytes and the current event object is 16 bytes.

The 2026-07-24 K3 one-shot/deadline build, based on
`8929c063cdd24c8f4f526be330549e2eb5038fc8-dirty`, reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 38,972 |
| `.data` | 0 |
| `.bss` | 8,336 |
| `.noinit` | 102,016 |
| interrupt-stack section including alignment and guard | 12,464 |
| worker MSP section including guard | 12,288 |
| 16 guarded thread supervisor-stack slots | 196,608 |
| total through `_kernel_memory_end` | 372,736 |
| flat kernel binary | 41,020 |

The normal binary SHA-256 is
`6ab38364d2ef5e67b6f5e8c7fb691cbf45291624562d7a0203f812c2e648e61d`.
The image leaves 151,552 bytes in the fixed 512 KiB reservation. K3 keeps each
thread record at 156 bytes and adds one fixed 258-byte deadline heap backing
store plus 20 bytes of deadline accounting. Including two alignment bytes,
the complete thread scheduler span is 2,952 bytes, 280 bytes above K2. No
deadline or timer path allocates memory.

The 2026-07-24 K4 handle-synchronization build reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 42,692 |
| `.data` | 0 |
| `.bss` | 9,592 |
| `.noinit` | 102,016 |
| interrupt-stack section including alignment and guard | 15,680 |
| worker MSP section including guard | 12,288 |
| 16 guarded thread supervisor-stack slots | 196,608 |
| total through `_kernel_memory_end` | 380,928 |
| flat kernel binary | 44,740 |

The exact hardware-qualified K4 binary SHA-256 is
`11c2ed31ca5caf07dcfbd87cf354f6ce7be3eb1873cef412b65a6821940fb91c`.
K4 adds 3,720 flat-binary bytes and 1,256 BSS bytes over K3. The image leaves
143,360 bytes in the fixed 512 KiB reservation. The fixed synchronization pool
accounts for 32 x 36-byte objects (1,152 bytes); the remaining BSS delta is
bounded pool state and diagnostics. Waiting reuses each thread's existing wait
link and deadline slot, so no synchronization or timeout path allocates memory.

The 2026-07-24 K5 thread-lifecycle qualification build reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 46,372 |
| `.data` | 0 |
| `.bss` | 10,152 |
| `.noinit` | 102,016 |
| interrupt-stack section including alignment and guard | 15,888 |
| worker MSP section including guard | 12,288 |
| 16 guarded thread supervisor-stack slots | 196,608 |
| total through `_kernel_memory_end` | 385,024 |
| flat kernel binary | 48,420 |

The exact hardware-qualified K5 binary SHA-256 is
`260bbcf82fbf955cee42d5798054e6d6549daa8921462d7216a241a685095e03`.
K5 adds 3,680 flat-binary bytes and 560 BSS bytes over K4. The image ends at
`0x0206e000` and leaves 139,264 bytes in the fixed 512 KiB kernel reservation.
The measured MC68030 layouts are 180 bytes per `KernelThread` and 450 bytes per
`KernelProcess`; compile-time assertions require both documents to change if
either layout moves. The 16 records therefore consume 2,880 thread bytes and
the four process records consume 1,800 bytes. Three new 36-byte lifecycle
performance records account for 108 BSS bytes. The remaining increase is
bounded reap state, accounting, and alignment; no lifecycle wait or exit path
allocates memory.

The exact hardware-qualified 2026-07-25 K6 build reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 55,364 |
| `.data` | 0 |
| `.bss` | 12,648 |
| `.noinit` | 102,016 |
| interrupt-stack section including alignment and guard | 16,336 |
| worker MSP section including guard | 12,288 |
| 16 guarded thread supervisor-stack slots | 196,608 |
| total through `_kernel_memory_end` | 397,312 |
| flat kernel binary | 57,412 |

The image ends at `0x02071000` and leaves exactly 126,976 bytes in the fixed
512 KiB kernel reservation. Its flat-binary SHA-256 is
`17476aa268db37dde0e066f4cc0799848bc0024ae12bba809ec8cffedf84f425`.
K6 reserves 2,048 BSS bytes for `16 x 16`
intrusive wait registrations at eight bytes each. Registrations are partitioned
by thread slot and require no allocator metadata. The measured MC68030 layouts
are 172 bytes per `KernelThread` and 596 bytes per `KernelProcess`; compile-time
assertions reject silent movement. The 16 thread records consume 2,752 bytes
and four process records consume 2,384 bytes. The process record grew from
472 to 596 bytes across the loader (four bytes recording the executable span)
and block admission (four transfer-memory records and their page count). The shared synchronization pool
remains 32 x 36 bytes; timers add fixed deadline, heap, and position arrays
rather than a second object pool. No wait, timer, death, cancellation, close,
or expiry path allocates memory. The structural sizes are compile-time asserted.

## K7 implemented static budget

K7 consumes at most 20 KiB of incremental fixed state. Compile-time MC68030
assertions and the exact qualified Beast build measure:

| Object | Limit | Measured bytes |
|---|---:|---:|
| port records with two embedded wait queues | 16 x 64 | 1,024 B |
| copied message records including 280 data bytes | 32 x 324 | 10,368 B |
| generation-safe detached authority records | 256 | 6,144 B |
| per-thread failed-send probe fields | 16 x 8 | 128 B |
| pool statistics and corruption latches | fixed | 208 B |
| total incremental fixed state | | 17,872 B |
| remaining against ceiling | | 2,608 B |

Queue payload storage is fixed and charged logically to the receiving port
owner; no heap allocation is hidden in these numbers. `KernelPort`,
`KernelPortMessage`, `KernelDetachedEntry`, and `KernelThread` sizes are
compile-time asserted. The failed-send probe is included in the 180-byte thread
record and owns no separate object reference.

The exact hardware-qualified 2026-07-25 K7 build reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 67,448 |
| `.data` | 0 |
| `.bss` | 30,600 |
| `.noinit` | 102,016 |
| interrupt-stack section including alignment and guard | 14,976 |
| worker MSP section including guard | 12,288 |
| 16 guarded thread supervisor-stack slots | 196,608 |
| total through `_kernel_memory_end` | 425,984 |
| flat kernel binary | 69,496 |

The image ends at `0x02078000` and leaves exactly 98,304 bytes in the fixed
512 KiB reservation. Its flat-binary SHA-256 is
`4c9d3807c95a701d8e2f16f52b1321fd97c0393798656ea589e6197c9dfccd4e`.
K7 adds 12,084 flat-binary bytes and 17,952 BSS bytes over K6. Of the BSS
increase, 17,872 bytes are the asserted port/message/detached-authority state,
72 bytes are the two additional 36-byte performance records, and eight bytes
are bounded accounting/alignment. No port, message, transfer, backpressure,
close, or peer-death path allocates memory.

## K8 implemented static budget

The exact hardware-qualified MC68030 K8 release build reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 79,720 |
| `.data` | 0 |
| `.bss` | 42,216 |
| `.noinit` | 102,016 |
| interrupt-stack section including alignment and guard | 15,664 |
| worker MSP section including guard | 12,288 |
| 16 guarded thread supervisor-stack slots | 196,608 |
| total through `_kernel_memory_end` | 450,560 |
| flat kernel binary | 81,768 |

The image ends at `0x0207e000` and leaves exactly 73,728 bytes in the fixed
512 KiB kernel reservation. K8 adds 12,272 flat-binary bytes and 11,616 BSS
bytes over K7. Target-only assertions freeze the 100-byte area record,
24-byte mapping record, 80-byte ring record, 28-byte handle entry, 28-byte
detached-authority entry, 536-byte process record, and unchanged 180-byte
thread record.

The complete BSS delta is accounted as follows:

| Incremental K8 state | Bytes |
|---|---:|
| 8 area records | 800 |
| 32 area-mapping records | 768 |
| 16 ring records with two embedded wait queues | 1,280 |
| byte-per-frame mapping class/count ledger growth | 7,168 |
| larger handle entries inside four process records | 256 |
| larger detached-authority entries | 1,024 |
| four additional performance records | 144 |
| area/ring pool statistics and corruption latches | 126 |
| bounded accounting and alignment | 50 |
| total BSS increase | 11,616 |

Area payload pages are real physical commit rather than static kernel state.
The development profile caps them at 128 pages system-wide, 64 pages per
creator, and 16 pages per area. Page tables are charged to the mapping address
space. Rings allocate no payload storage beyond their owning area and reuse
the existing fixed wait-registration pool.

The exact flat-binary SHA-256 is
`ea879e760c48342f535ee9aee65bf1bab97e855c2e576579c2ab80ef615ba55b`.

## K9 implemented static budget

The exact hardware-qualified MC68030 K9 release build reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 88,084 |
| `.data` | 0 |
| `.bss` | 43,800 |
| `.noinit` | 111,232 |
| interrupt-stack section including alignment and guard | 12,880 |
| worker MSP section including guard | 12,288 |
| 16 guarded thread supervisor-stack slots | 196,608 |
| total through `_kernel_memory_end` | 466,944 |
| flat kernel binary | 90,132 |

The image ends at `0x02082000` and leaves exactly 57,344 bytes in the fixed
512 KiB kernel reservation. K9 adds 8,364 flat-binary bytes, 1,584 BSS bytes,
and 9,216 no-init bytes over K8. Linker alignment makes that 10,800-byte
fixed-state addition consume four additional 4 KiB pages in the reserved
kernel region.

The complete fixed-state delta is accounted as follows:

| Incremental K9 state | Bytes |
|---|---:|
| per-frame stable allocation-site IDs | 8,192 |
| emergency-reserve membership bitmap | 1,024 |
| 11 typed-cache descriptors | 286 |
| typed-cache allocation bitmaps | 76 |
| site records, subsystem aggregates, and injection selector | 1,200 |
| target alignment | 22 |
| total fixed-state increase | 10,800 |

Exactly 32 ordinary physical frames, 131,072 bytes, are transferred into the
emergency reserve during memory initialization. This is committed physical
capacity rather than image or static-object storage. It changes the normal
post-boot free count from K8's 7,992 pages to 7,960 pages while reserve
diagnostics independently report 32/32 available. The exact 1,000-iteration
K9 workload returns to a 7,954-page live-workload baseline after every
iteration.

The flat-kernel SHA-256 is
`6d9397b044e133bb9e04750d78cd46e3b32ea1e418d553e44c9e97f958b6d823`;
the 124,028-byte ELF SHA-256 is
`293261fef2378ae767d3f1f6edb4730679fd40ccec28a2dcc8ab944366d959b0`.

Major current static objects are:

| Object | Count x size | Bytes |
|---|---:|---:|
| frame metadata | 8,192 x 8 | 65,536 |
| per-frame owner links | 2 x 8,192 x 2 | 32,768 |
| owner ledgers | 64 x 8 | 512 |
| allocator bitmaps | 3 x 1,024 | 3,072 |
| process slots | 7 x 1,188 | 8,316 |
| thread records | 16 x 180 | 2,880 |
| ready queues | 32 x two 16-bit heads/tails plus bitmap/count | 136 |
| deadline arrays | positions 32 + heap 32 + results 64 + cycles 128 + count 2 | 258 |
| wait registrations | 256 x 8 | 2,048 |
| waitable-timer ordering | deadlines 256 + heap 32 + positions 32 + count | 321 |
| guarded thread supervisor-stack arena | 16 x 12 KiB | 196,608 |
| synchronization objects | 32 x 36 | 1,152 |
| message-port objects | 24 x 64 | 1,536 |
| copied message records | 72 x 324 | 23,328 |
| detached authority records | 256 x 28 | 7,168 |
| shared-area records | 8 x 100 | 800 |
| shared-area mapping records | 32 x 24 | 768 |
| bulk-ring records | 16 x 80 | 1,280 |
| performance metric records | 20 x 36 | 720 |
| typed-cache descriptors and bitmaps | 286 + 76 | 362 |
| frame allocation-site ledger | 8,192 x 1 | 8,192 |
| emergency-reserve membership bitmap | 8,192 bits | 1,024 |
| allocation sites, subsystem totals, and selector | fixed | 1,200 |
| DMA slots | 32 x 36 | 1,152 |
| block slots | 4 x 64 | 256 |
| cached-user-frame class/count ledger | 8,192 x 1 | 8,192 |
| boot-info copy | 1 x 256 | 256 |

The interactive GUI profile is the exact reason for the current seven-process
ceiling: supervisor, storage, events, input, display, terminal, and one
foreground command. The per-process handle proof is 37 entries and still uses
two 32-bit bitmap words. Relative to the preceding six-process/36-handle
profile, the process array grows by 1,356 bytes: one 1,188-byte process record
plus 28 bytes in each of the six existing records. Shared-area alias capacity
is seven for the same measured composition.

## K10 implemented candidate budget

The current pre-route MC68030 K10 build reports:

| ELF section | Bytes |
|---|---:|
| `.text.entry` | 80 |
| `.vectors` | 1,024 |
| `.text` plus read-only data | 119,412 |
| `.data` | 0 |
| `.bss` | 49,768 |
| `.noinit` | 111,232 |
| interrupt-stack section including alignment and guard | 14,736 |
| worker MSP section including guard | 12,288 |
| 16 guarded thread supervisor-stack slots | 196,608 |
| `.kernel_trace` | 65,536 |
| reserved span through `_kernel_memory_end` | 589,824 |
| flat kernel binary | 121,460 |

The bootstrap portion ends at `0x0208B000`, leaving 20,480 bytes in the fixed
512 KiB bootstrap reservation. The retained trace occupies exactly
`0x02090000..0x0209FFFF`; BootInfo reserves the contiguous 576 KiB kernel range
through `0x0209FFFF`, and allocatable RAM resumes at `0x020A0000`. The memory
initializer therefore reports 7,948 ordinary free pages plus the separate
32-page emergency reserve. The target K10 workload reports 7,944 ordinary free
pages after its two qualification processes are established.

Relative to exact K9, K10 adds 31,328 flat-binary bytes and 5,968 BSS bytes;
`.noinit` is unchanged. The BSS increase is fully assigned as follows:

| Incremental K10 fixed state | Bytes |
|---|---:|
| 16 IRQ endpoints, four embedded records each | 2,048 |
| 32 Vesta source-route records | 896 |
| 32 pre-PMMU staged trace records | 896 |
| two bounded monitor transport channels | 788 |
| six additional performance records | 216 |
| eight deferred-class callback/context/counter tables | 128 |
| controller, fault, monitor, latency state, and alignment | 444 |
| 32-entry claimed-interrupt handoff ring | 512 |
| claimed-interrupt indices, counters, and alignment | 40 |
| total BSS increase | 5,968 |

The 64 KiB trace is allocation-free fixed storage outside `.bss`; it is not
charged again to the heap or physical-frame allocator. IRQ endpoints consume
128 bytes per live slot from their fixed typed cache and have no separate
record allocation. This accounting is implementation evidence only. Exact
committed hashes, route resources, and hardware promotion remain pending.

## PMMU table cost

Current 4 KiB `10/10/12` tables use 4-byte short descriptors:

- one 4 KiB SRP root;
- one 4 KiB low-SDRAM/stack-guard leaf;
- one 4 KiB supervisor MMIO leaf;
- one 4 KiB empty CRP root;
- one 4 KiB CRP root plus one 4 KiB leaf per populated 4 MiB user region;
- K1 process with code and stack in separate regions: 12 KiB/process.

The implemented common charge is 16 KiB. With four populated K1 processes, the
maximum live charge is 16 KiB common plus 48 KiB of process tables.

The proposed 8 KiB `9/10/13` option uses a 2 KiB root and 4 KiB leaves mapping
8 MiB each. Roots must come from a subpage table slab; charging an 8 KiB frame
per 2 KiB root would erase much of the expected saving.

## Stable object-pool target

These are hard maximum backing budgets for the first stable kernel. Individual
objects are allocated from typed pools only when live.

| Pool | Limit | Budget |
|---|---:|---:|
| process records, excluding handles | 32 x 192 B | 6 KiB |
| thread records | 128 x 192 B maximum | 24 KiB |
| handle entries | 8,192 x 16 B | 128 KiB |
| areas | 512 x 64 B | 32 KiB |
| ports | 256 x 64 B | 16 KiB |
| message headers | 1,024 x 32 B | 32 KiB |
| copied message payload pool | fixed | 512 KiB |
| semaphore/event records | 256 x 32 B | 8 KiB |
| timer records | 256 x 48 B | 12 KiB |
| IRQ/device records | 64 x 64 B | 4 KiB |
| trace ring | fixed | 64 KiB |
| bounded general kernel heap | hard cap | 256 KiB |
| emergency reserve | 32 x 4 KiB | 128 KiB |

Target pool backing totals 1,222 KiB including the heap and emergency reserve.
Changing a structure size or count requires updating this table and a generated
build report before merge.

## Stack budget

- K1 interrupt stack (ISP): 8 KiB plus one unmapped 4 KiB guard.
- K1 deferred-worker master stack (MSP): 8 KiB plus one unmapped 4 KiB guard.
- K9 user stack: one mapped 4 KiB page per live thread, with adjacent stack
  bases spaced by 8 KiB so every stack has an unmapped 4 KiB guard interval.
  The 1,000-iteration K9 soak retains an exact 7,954-free-page live-workload
  baseline after every teardown.
- Current K9 supervisor stack: one guarded 8 KiB stack for each of 16 thread
  slots. The static arena reserves 192 KiB, of which 128 KiB is mapped stack
  payload and 64 KiB is unmapped guard space. Full RTL and both routed-hardware
  boots report a maximum observed use of 680 bytes.
- Stable per-thread kernel stack target: 8 KiB committed only for a live thread
  plus an unmapped virtual guard; replacing the fixed development arena must
  preserve the same fault and high-water tests.
- 128 simultaneous thread stacks would consume 1 MiB; ordinary process quotas
  prevent every process from reaching the global thread limit independently.
- K9 user threads enter on their own guarded supervisor stacks. Public
  `WAIT_ONE` and `WAIT_MULTIPLE` block on events, semaphores, timers, thread
  death, or process death with an absolute deadline and fixed registrations.

K9 charges thread resources in distinct units rather than hiding them in a
single count:

| Charge | Per retained/live thread | Release point |
|---|---:|---|
| thread object | one 180-byte global slot | death plus final handle close |
| user stack | one 4 KiB physical frame while live | deferred worker after death |
| user guard | one unmapped 4 KiB logical page while live | deferred worker after death |
| supervisor stack | two 4 KiB pages from the fixed arena | death plus final handle close |
| supervisor guard | one unmapped 4 KiB page from the fixed arena | death plus final handle close |
| handle | one 28-byte development entry in the owning process | `CLOSE` or process teardown |

The K9 qualification cap is 16 global thread objects and 15 per process. A
dead thread with an open handle intentionally retains 12 KiB of the fixed
supervisor arena and its record, but no user physical page. These retained
charges are bounded and visible; they are not classified as leaks.

Every build must emit static stack-usage data. Hardware qualification records
canary and high-water values under nested format-B faults and interrupt storms.

## Graphics arena

Graphics memory is excluded from general kernel/free-process statistics once
the boot arena is carved. The Arty target reserves the contiguous 128 MiB range
`0x18000000..0x1fffffff` from its 512 MiB DDR as `no-map`; Linux and Axiom use
the lower 384 MiB as general memory. The physical address is board-specific and
is not part of the application ABI.

Baseline double-buffered 1280x720 RGB565 scanout is:

```text
1280 * 720 * 2 bytes/pixel * 2 buffers = 3,686,400 bytes
```

One framebuffer is 1,843,200 bytes (1.76 MiB). The version-1 maximum scrolling
surface is 2560x1440; two RGB565 rings consume 14,745,600 bytes (14.06 MiB),
while two XRGB8888 rings consume 29,491,200 bytes (28.13 MiB). A surface is
always charged by `virtual_width * virtual_height * bytes_per_pixel *
buffer_count`.

Sprite INDEX8 images are charged separately. One maximum 128x128 image is
16 KiB and 64 distinct maximum images are exactly 1 MiB. The arena allocator
holds a 2 MiB reclaimable sprite reserve, enough for simultaneous maximum
ACTIVE and PENDING scenes. This is a capacity guarantee rather than a fixed
address partition; animation caches can borrow all otherwise-unpinned arena
memory. Descriptor metadata and the four active palette replicas reside in PL
memory and are reported in FPGA resource accounting, not DDR arena usage.

Command rings, completion rings, tile maps and patterns, engine workspaces,
sprite images, and user-visible surfaces retain distinct owner, current, peak,
and pinned-byte charges even though they share the arena allocator.

## Commit and quota defaults

With 4 KiB pages:

| Charge | Ordinary process | System total |
|---|---:|---:|
| committed private/shared pages | 2,048 pages (8 MiB) | physical availability |
| pinned pages | 256 pages (1 MiB) | 2,048 pages (8 MiB) |
| queued copied IPC bytes | 256 KiB | 512 KiB |
| threads | 16 | 128 |
| handles | 256 | 8,192 |

System services receive explicit resource-account quotas. No account may
reserve more physical commit than exists after kernel, ROM, graphics, emergency,
and wired-device reservations.

## Low-memory gates

Allocation sites are tagged by subsystem. Diagnostics report current, peak,
failure count, and owner. Qualification must prove that after cache reclaim and
ordinary allocation rejection, the emergency reserve can still complete one
maximum fault, close a maximum-size handle table, drain peer-death IPC, write a
panic record, and keep input/display/debugger services schedulable.

K9 implements and qualifies the kernel mechanism: all 22 external allocation
sites are deterministically injectable, zero-free user-fault/process cleanup
returns every object and frame to baseline, all 32 reserve pages remain
isolated from ordinary allocation, and retained logging remains
allocation-free. Disposable user-service caches, pressure notification, input
and display service prioritization, and offender termination policy remain
future userspace/kernel policy and are not claimed by this gate.
