# Astra 68 memory map and PMMU contract

Status: normative VM contract, revision 0.1 (2026-07-24)

`MEMORY_MAP.md` remains the authoritative physical aperture registry. This
document defines how the kernel translates, owns, and changes those ranges.

## Physical memory

The physical aperture and boot-reservation table lives only in
`MEMORY_MAP.md`. Boot ranges come from validated `AstraBootInfo`; the allocator
does not infer ownership from linker symbols. Frame metadata is sized from the
reported guest map and carved from its largest usable range.

## Logical spaces

### Supervisor (SRP)

- Kernel SDRAM is identity mapped across the RAM ranges reported by validated
  BootInfo except for the ISP and deferred-worker MSP guard pages selected by
  the linker.
- Required Vesta, Astraea, Vega, and OHCI pages are identity mapped
  cache-inhibited in `0xFFF00000..0xFFF40FFF`.
- Vectors, exception code, active ISP/MSP stacks, page tables, frame
  metadata, panic console, and early log are permanently resident.
- TT0 and TT1 remain disabled. Transparent translation bypasses ordinary
  descriptor protection, has a minimum 16 MiB aperture, and is not a shortcut
  for user-visible MMIO, framebuffer, or shared-memory mappings. Those ranges
  use normal supervisor descriptors with explicit permissions and cache policy.

Reset is not treated as an ATC invalidation guarantee. Before enabling
translation, boot explicitly writes disabled TC and ITT0/ITT1/DTT0/DTT1,
installs SRP and the initial URP, sets SFC/DFC, executes `PFLUSHA`, invalidates
and pushes the instruction and data caches, and only then loads enabled TC.
QEMU is the sole processor implementation and its MC68040 MMU behavior is
covered by the kernel and machine qualification suites.

### User (one URP per process)

- Valid user range is `0x00010000..0x7FFFFFFF`; null and the first 64 KiB are
  always unmapped.
- K1 maps one read/execute page at `0x00100000` and one read/write stack page at
  `0x70000000`; adjacent pages are unmapped guards.
- User URP trees never map kernel, page-table, firmware, ROM-control, or MMIO
  frames. Privileged device mappings are a later explicit object type.
- Threads in one process share a URP. Switching between them does not reload
  URP or flush the ATC.

The current MMU configuration uses native MC68040 three-level descriptors with
a `7/7/6/12` split and separate SRP/URP roots. A root covers 4 GiB, each pointer
table entry covers 256 KiB, and each 64-entry page table maps 256 KiB with 4 KiB
pages. Axiom allocates each table from one zeroed, owner-charged 4 KiB frame so
publication, rollback, and release use the ordinary frame allocator.

## Page-size decision

**LOCKED:** 4 KiB is the MC68040 page size selected by TC and the only supported
VM geometry. Fault, stacking, user-copy, teardown, and hardware qualification
all exercise that exact configuration.

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

MC68040 page descriptors enforce write protection but not execute-disable. W^X
is kernel policy and must not be described as hardware NX.

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
   MC68040 `PFLUSH` operation before returning success.
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

The current implementation uses address-selective `PFLUSH` for one-page
descriptor changes and `PFLUSHA` for wider transitions. A cross-URP switch
loads URP and flushes the ATC; a same-URP switch does neither. Cache maintenance
is reserved for transitions that require it rather than every address-space
switch.

## Cache and alias policy

One physical frame has one cache policy across all logical aliases:

| Use | Policy |
|---|---|
| ordinary code/data | cacheable, write-through data |
| MMIO | cache-inhibited |
| framebuffer/graphics command memory read by hardware | cache-inhibited until explicit maintenance is proven |
| DMA shared with ESP, USB, graphics, or audio | cache-inhibited or explicit ownership transfer plus cache maintenance |
| page tables | kernel cacheable, never DMA-visible |

The MC68040's caches are physically addressed, so one shared-area frame may be
mapped at different logical slots in different processes. A runtime-sized
per-frame ledger still enforces mapping class and alias-count invariants. The
permanent supervisor physical map is deliberate: code/data is written through
it only before user publication or under explicit ownership transfer, followed
by required cache maintenance before user access. Loading executable bytes
pushes data and invalidates instruction cache before entry.

The current K1 target test gives two processes different instruction bytes and
different stack markers at identical logical addresses. Both caches are enabled;
the offender dies at its planned unmapped access while the survivor continues.
The qualification image passes on the QEMU MC68040/MMU path.

## Guards and residency

- **CURRENT:** null, user code edges, and both sides of each fixed user stack
  are unmapped.
- **CURRENT:** the 8 KiB interrupt stack and 8 KiB deferred-worker master stack
  are fixed and bounded. Each has a linker-selected 4 KiB page immediately
  below it that is invalid in the low SRP leaf; adjacent identity pages remain
  valid. Host tests inspect both exact descriptors, and normal plus soak target
  images run with this tree enabled.
- A kernel guard fault during stacking is fatal; recovery is not attempted.
- **CURRENT:** a deliberate access enters vector 2 with an MC68040 format-7
  supervisor access-error frame, prints the exact address, sets retained panic
  state, and reaches the full-SoC panic oracle. Hardware repetition remains.
- The only supported MC68040 frames are formats 0, 1, 2, 3, and 7; the maximum
  is 60 bytes. The worker stack has a bottom canary,
  debug poison, and exact high-water reporting. Equivalent per-user-thread
  kernel-stack accounting remains stable-kernel work in `STATUS.md`.

## No swapping or overcommit

Every anonymous, shared, page-table, pinned, and kernel page is charged to
physical commit before success. There is no disk swap. Demand-zero, clean file
page reclamation, and shared executable pages may be added only after map,
unmap, cache, and low-memory fault injection are stable.
