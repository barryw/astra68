# Astra 68 kernel memory budget

Status: measured K1 baseline plus bounded revision-0.1 targets (2026-07-22)

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

Major K1 static objects are:

| Object | Count x size | Bytes |
|---|---:|---:|
| frame metadata | 8,192 x 8 | 65,536 |
| allocator bitmaps | 3 x 1,024 | 3,072 |
| process slots | 4 x 528 | 2,112 |
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

- K1 bootstrap supervisor stack: 8 KiB plus one unmapped 4 KiB guard.
- Stable per-thread kernel stack target: 8 KiB committed only for a live thread
  plus an unmapped virtual guard.
- 128 simultaneous thread stacks would consume 1 MiB; ordinary process quotas
  prevent every process from reaching the global thread limit independently.
- IRQ/deferred stack strategy (ISP/MSP versus per-thread M=0) remains a release
  blocker. Whichever is selected gets a fixed 16 KiB maximum and guard.

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
