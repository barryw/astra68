# Astra 68 kernel memory budget

Status: measured K2 blocking/thread baseline plus bounded revision-0.1 targets (2026-07-24)

The machine has exactly 32 MiB of SDRAM. Every static pool, frame, mapping,
queue, pin, and graphics reservation is reported separately. A budget is not
permission to allocate dynamically without a quota.

## Physical baseline

| Reservation | Bytes |
|---|---:|
| early retained log | 16,384 |
| kernel load reservation | 524,288 |
| ROM backing | 262,144 |
| remaining before future graphics/device carveouts | 32,751,616 |

The detailed split and bootstrap BRAM are in `MEMORY_MAP.md`. The kernel starts
from BootInfo ranges, not the arithmetic above.

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

Major current static objects are:

| Object | Count x size | Bytes |
|---|---:|---:|
| frame metadata | 8,192 x 8 | 65,536 |
| per-frame owner links | 2 x 8,192 x 2 | 32,768 |
| owner ledgers | 64 x 8 | 512 |
| allocator bitmaps | 3 x 1,024 | 3,072 |
| process slots | 4 x 446 | 1,784 |
| thread scheduler state | 16 x 156 plus queues/accounting | 2,672 |
| guarded thread supervisor-stack arena | 16 x 12 KiB | 196,608 |
| performance metrics/control | 8 x 36 plus control | 296 |
| DMA slots | 32 x 36 | 1,152 |
| block slots | 4 x 64 | 256 |
| cached-user-frame alias ledger | 8,192 bits | 1,024 |
| boot-info copy | 1 x 256 | 256 |

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
| thread records | 128 x 160 B | 20 KiB |
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

Target pool backing totals 1,218 KiB including the heap and emergency reserve.
Changing a structure size or count requires updating this table and a generated
build report before merge.

## Stack budget

- K1 interrupt stack (ISP): 8 KiB plus one unmapped 4 KiB guard.
- K1 deferred-worker master stack (MSP): 8 KiB plus one unmapped 4 KiB guard.
- K2 user stack: one mapped 4 KiB page per live thread, with adjacent stack
  bases spaced by 8 KiB so every stack has an unmapped 4 KiB guard interval.
  The 1,000-cycle K2 soak holds two survivor stacks and repeatedly allocates a
  third, retaining an exact 7,986-free-page baseline after every teardown.
- Current K2 supervisor stack: one guarded 8 KiB stack for each of 16 thread
  slots. The static arena reserves 192 KiB, of which 128 KiB is mapped stack
  payload and 64 KiB is unmapped guard space. Full RTL and both routed-hardware
  boots report a maximum observed use of 388 bytes.
- Stable per-thread kernel stack target: 8 KiB committed only for a live thread
  plus an unmapped virtual guard; replacing the fixed development arena must
  preserve the same fault and high-water tests.
- 128 simultaneous thread stacks would consume 1 MiB; ordinary process quotas
  prevent every process from reaching the global thread limit independently.
- K2 user threads enter on their own guarded supervisor stacks. The internal
  qualification event blocks and wakes across syscalls; no stable blocking
  user ABI is exposed yet.

Every build must emit static stack-usage data. Hardware qualification records
canary and high-water values under nested format-B faults and interrupt storms.

## Graphics arena

Graphics memory is excluded from general kernel/free-process statistics once
the boot arena is carved. Baseline double-buffered 720x480 RGB565 scanout is:

```text
720 * 480 * 2 bytes/pixel * 2 buffers = 1,382,400 bytes
```

One framebuffer is 691,200 bytes (675 KiB). A wider scrolling surface is
charged by `virtual_width * virtual_height * bytes_per_pixel * buffer_count`.
The arena size is **not selected yet**; `STATUS.md` must remain MISSING until
surface workloads, contiguity, and INDEX8/RGB565 mixes are measured. Command
rings, palettes, sprites, and user-visible surfaces receive separate charges.

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
