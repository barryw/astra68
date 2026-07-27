# Astra 68 memory map and PMMU contract

Status: normative VM contract, revision 0.1 (2026-07-24)

`MEMORY_MAP.md` remains the authoritative physical aperture registry. This
document defines how the kernel translates, owns, and changes those ranges.

## Physical memory

| Physical range | Size | Kernel treatment |
|---|---:|---|
| `0x02000000..0x02003FFF` | 16 KiB | wired early log |
| `0x02004000..0x0200FFFF` | 48 KiB | allocatable after handoff |
| `0x02010000..0x0208FFFF` | 512 KiB | wired kernel bootstrap reservation |
| `0x02090000..0x0209FFFF` | 64 KiB | wired retained trace |
| `0x020A0000..0x03DFFFFF` | 29.375 MiB | allocatable |
| `0x03E00000..0x03E3FFFF` | 256 KiB | wired ROM backing |
| `0x03E40000..0x03FFFFFF` | 1.75 MiB | allocatable |
| `0xFFE00000..0xFFE3FFFF` | 256 KiB | read/execute ROM alias |
| `0xFFF00000..0xFFF4FFFF` | device pages | supervisor, cache-inhibited |

Boot ranges come only from validated `AstraBootInfo`; the allocator does not
infer ownership from linker symbols. Every one of the 8,192 current 4 KiB
frames has an 8-byte owner/reference/pin/state record.

## Logical spaces

### Supervisor (SRP)

- Kernel SDRAM is identity mapped at `0x02000000..0x03FFFFFF` except for the
  ISP and deferred-worker MSP guard pages selected by the linker.
- Required Vesta, Astraea, Vega, and OHCI pages are identity mapped
  cache-inhibited in `0xFFF00000..0xFFF40FFF`.
- Vectors, exception code, active ISP/MSP stacks, page tables, frame
  metadata, panic console, and early log are permanently resident.
- TT0 and TT1 remain disabled. Transparent translation bypasses ordinary
  descriptor protection, has a minimum 16 MiB aperture, and is not a shortcut
  for user-visible MMIO, framebuffer, or shared-memory mappings. Those ranges
  use normal supervisor descriptors with explicit permissions and cache policy.

Reset is not treated as an ATC invalidation guarantee. Before enabling
translation, boot explicitly writes disabled TC and TT0/TT1, installs SRP and
the initial CRP, sets SFC/DFC, executes `PFLUSHA`, independently invalidates the
instruction and data caches through CACR, and only then loads enabled TC. A
Motorola-directed RTL test covers simultaneous CACR I/D command decode even
though the kernel deliberately emits the two invalidations separately.
This follows MC68030 User's Manual section 9.2.2, which explicitly requires an
ATC-flushing MMU instruction after reset and before translation is enabled.

**CURRENT RTL:** source `c599f921cb35dcc7e8d2988ba253769341311516`
separates deterministic ECP5 configuration initialization from architectural
processor reset. One configuration-initialized bit clears scalar PMMU state and
invalidates the ATC on the first released clock but is never re-armed by
processor reset. MC68030 `RESET` clears only TC.E, TT0.E, and TT1.E; it
preserves CRP, SRP, the other control fields, and valid ATC entries. A focused
Motorola-directed test retains a deliberately stale translation across reset,
observes it with level-zero `PTEST`, executes `PFLUSHA` while TC.E is clear,
then proves that re-enabling translation walks to the changed descriptor. The
complete strict Questa inventory is 141 total and 115 clean with the prior
3 compile, 18 simulation, and 5 unscored buckets unchanged. GHDL 7.0 generates
the complete core and a byte-identical pre-commit reduced-BIST full-SoC run
passes POST, protected multitasking, offender-only fault containment, and four
lifecycle soak cycles. The prior exact full-chip route and board reset/boot
qualification pass; guarded-worker source `42f4bb55...` requires a new exact
route and board promotion.

### User (one CRP per process)

- Valid user range is `0x00010000..0x7FFFFFFF`; null and the first 64 KiB are
  always unmapped.
- K1 maps one read/execute page at `0x00100000` and one read/write stack page at
  `0x70000000`; adjacent pages are unmapped guards.
- User CRP trees never map kernel, page-table, firmware, ROM-control, or MMIO
  frames. Privileged device mappings are a later explicit object type.
- Threads in one process share a CRP. Switching between them does not reload
  CRP or flush the ATC.

The current PMMU configuration is 4 KiB, two-level short descriptors with a
`10/10/12` split and separate SRP/CRP roots. A user root is 4 KiB; each leaf is
4 KiB and maps 4 MiB. K1's code and stack therefore cost one root plus two
leaves, or 12 KiB of table memory per process. Common bootstrap translation
cost is 16 KiB: one SRP root, one leaf for the low SDRAM/stack-guard region,
one high-MMIO leaf, and one empty CRP root.

## Page-size decision

**CURRENT:** the K1 qualification image uses 4 KiB. Existing fault, stacking,
user-copy, and teardown evidence is tied to that configuration and remains the
comparison oracle.

**LOCKED TARGET:** add an 8 KiB `9/10/13` build configuration and make it the
stable default after it passes identical host, Musashi, RTL, route, and board
gates. Keep 4 KiB as a supported build option. The selection report includes:

| Metric | 4 KiB | 8 KiB |
|---|---:|---:|
| physical frames in 32 MiB | 8,192 | 4,096 |
| metadata at 8 bytes/frame | 64 KiB | 32 KiB |
| root descriptor bytes | 4,096 | 2,048 |
| leaf descriptor bytes | 4,096 | 4,096 |
| address span per leaf | 4 MiB | 8 MiB |
| internal fragmentation | measured | measured |
| ATC reach/miss rate | measured | measured |
| map/unmap and CRP-switch cycles | measured | measured |

The 22-entry ATC reaches at most 88 KiB with 4 KiB pages and 176 KiB with 8 KiB
pages. The 8 KiB implementation also needs a bounded table-slab allocator so a
2 KiB root does not consume an otherwise unusable 8 KiB frame. It is not
accepted merely because those static numbers improve; the table walker, fault
frames, internal fragmentation, and application working sets must agree.

## Descriptor ownership

Only `vm.c` and architecture PMMU primitives may read or write descriptors.
No driver, service, allocator, or debugger edits a table directly. Active
table pages are kernel-owned, wired, non-DMA, and never user-mapped.

For every mapping, the VM records:

- address-space owner and area object;
- logical range and physical frames;
- read, write, software-execute, and cache policy;
- mapping and pin/reference counts;
- whether hardware or another process may access the frame.

MC68030 short descriptors enforce write protection but not execute-disable.
W^X is kernel policy and must not be described as hardware NX.

## Mapping transitions

All transitions execute under the address-space VM lock. No user pointer is
retained across a transition.

### Map

1. Validate range addition, alignment, ownership, commit charge, rights, and
   uniform cache policy.
2. Allocate and zero any leaf before publishing its root descriptor.
3. Retain every physical frame.
4. Write the final page descriptor once.
5. Complete required cache synchronization, then perform the documented
   Motorola `PFLUSH` operation before returning success.
6. Publish area/accounting state only after translation is usable.

Any failure unwinds in reverse order and leaves no descriptor reachable.

### Protect or unmap

1. Mark the area closing so no new pin or lookup can succeed.
2. Synchronize dirty data and invalidate logical cache aliases as required.
3. Reduce permissions or clear the descriptor.
4. Invalidate the required logical cache state, then perform the required
   `PFLUSH` before a frame can be reassigned.
5. Wait in thread context for existing pins/references; never wait in IRQ.
6. Release empty leaves, commit charge, and frames.

The current K1 implementation uses a conservative full ATC flush after each
descriptor change and CRP reload. Mapping publication and removal invalidate
both logical caches first. A cross-CRP switch invalidates both caches, loads
CRP, then executes `PFLUSHA`; a same-CRP switch does neither. Address-selective
maintenance is an optimization only after exact Motorola and RTL tests.

## Cache and alias policy

One physical frame has one cache policy across all logical aliases:

| Use | Policy |
|---|---|
| ordinary code/data | cacheable, write-through data |
| MMIO | cache-inhibited |
| framebuffer/graphics command memory read by hardware | cache-inhibited until explicit maintenance is proven |
| DMA shared with ESP, USB, graphics, or audio | cache-inhibited or explicit ownership transfer plus cache maintenance |
| page tables | kernel cacheable, never DMA-visible |

Two simultaneous user cached logical aliases are rejected by a fixed 1,024-byte
ledger containing one bit for each physical frame. The permanent supervisor
physical map is the one deliberate alias: code/data is written through it only
before user publication or under an explicit ownership transfer, followed by
full data/instruction-cache invalidation before user access. Loading executable
bytes requires data-cache synchronization, instruction-cache invalidation for
the destination logical range, and an ATC-valid mapping before entry.

The current K1 target test gives two processes different instruction bytes and
different stack markers at identical logical addresses. Both caches are enabled;
the offender dies at its planned unmapped access while the survivor continues.
The unchanged image passes on Musashi and the complete RTL cache/PMMU path.

## Guards and residency

- **CURRENT:** null, user code edges, and both sides of each fixed user stack
  are unmapped.
- **CURRENT:** the 8 KiB interrupt stack and 8 KiB deferred-worker master stack
  are fixed and bounded. Each has a linker-selected 4 KiB page immediately
  below it that is invalid in the low SRP leaf; adjacent identity pages remain
  valid. Host tests inspect both exact descriptors, and normal plus soak target
  images run with this tree enabled.
- A kernel guard fault during stacking is fatal; recovery is not attempted.
- **CURRENT SIM:** a deliberate access to `0x02028000` enters vector 2 with a
  format-A supervisor fault, prints the exact address, sets retained panic
  state, and reaches the full-SoC panic oracle. Hardware repetition remains.
- Exception-frame maximum is 92 bytes. The worker stack has a bottom canary,
  debug poison, and exact high-water reporting. Equivalent per-user-thread
  kernel-stack accounting remains stable-kernel work in `STATUS.md`.

## No swapping or overcommit

Every anonymous, shared, page-table, pinned, and kernel page is charged to
physical commit before success. There is no disk swap. Demand-zero, clean file
page reclamation, and shared executable pages may be added only after map,
unmap, cache, and low-memory fault injection are stable.
