# Astra storage and VFS implementation plan

Status: block facade implemented; protected storage stack not yet running

## First bootable objective

The first storage-capable Astra boot is complete only when this exact path
works without Linux performing Astra filesystem operations on behalf of the
guest:

```text
Arty Linux init
  -> Astra graphics splash and status rows
  -> Astra m68k machine
  -> Axiom
  -> protected supervisor
  -> block service + storage/VFS service + input service
  -> terminal shell
```

The shell must support at least `pwd`, `cd`, `ls`, `stat`, `cat`, `mkdir`,
`touch`, `cp`, `mv`, `rm`, `sync`, `mount`, `assign`, and `df`. These commands
use VFS client operations. They do not parse an on-disk format, invoke Linux,
or receive raw device authority.

The first terminal may be a single protected full-screen console rendered by
the display service. A window manager and final terminal Kit are not required
for this gate. Keyboard events still pass through the protected input service;
the terminal does not open Linux evdev or physical MMIO itself.

## Layering

```text
shell and applications
  VFS client Kit: paths, current directory, assigns, file handles
  storage/VFS service: mount table, namespace, permissions, object identity
  filesystem handler: FAT/exFAT | native writable | RAM
  synchronous block facade: validation, generations, deadlines, metrics
  service adapter: Axiom block lease | QEMU image | deterministic memory
  physical backend
```

The synchronous block facade is deliberate. A filesystem worker may issue one
operation and block on the asynchronous kernel request/IRQ completion while
other service workers remain schedulable. Backend adapters own request IDs,
DMA/bounce storage, IRQ waits, cancellation, and reset. Filesystem code never
polls a register or depends on the transport.

`sw/userspace/storage` currently implements this facade and a caller-owned
memory/image backend. It validates power-of-two 512 through 4096-byte sectors,
64-bit bounds without wraparound, bounded transfers, presence, read-only state,
media generations, and backend failures. Every query, read, write, and flush
records calls, failures, transferred sectors, total ticks, and maximum ticks.
The clock is injected: host tests use monotonic nanoseconds and Astra uses the
Vesta cycle counter.

## Block-service boundary

The existing Axiom block and DMA engines remain the hardware mechanism. They
are not exposed as raw global IDs. The supervisor transfers one block-device
lease and one completion IRQ endpoint to the protected block service.

The userspace admission work must provide:

1. a versioned geometry query with sector count/size, maximum transfer,
   capabilities, host generation, and media generation;
2. bounded read/write/flush submission using process-owned transfer memory;
3. a generation-safe request handle that is waitable or paired with the
   granted completion endpoint;
4. collection that commits read bytes exactly once and reports late,
   cancelled, reset, and media-changed completions distinctly;
5. teardown that revokes every in-flight request and returns all pinned pages;
6. hard per-service request, byte, pinned-page, and timeout limits.

The API must be fault-injected before use by a filesystem. No temporary polling
syscall or unrestricted physical-address argument is acceptable.

**IMPLEMENTED** at syscall ABI `0x0001000a`, and fault-injected before any
filesystem depends on it:

| # | Where |
|---|---|
| 1 | `BLOCK_QUERY` renders `AstraBlockLeaseInfo` from the transport: sector size and count, maximum transfer, capabilities, state flags, and both generations |
| 2 | `BLOCK_SUBMIT` takes an `AstraBlockRequest` naming a transfer-memory **handle** and an offset; the kernel resolves it to pages the caller never sees |
| 3 | The engine's request handles are generation-tagged, and the completion IRQ endpoint is granted to the same service at launch |
| 4 | `BLOCK_COLLECT` drains the transport, releases the request on collection so bytes commit exactly once, and reports OK, device error, reset, and media-changed distinctly |
| 5 | Process teardown already revoked in-flight requests and DMA; closing a transfer handle unmaps and returns its pages, and the engine defers reclaim while a transfer is in flight |
| 6 | 4 requests, 4 buffers, and 16 pinned pages per service, each a rejection rather than a stall; one buffer carries one transfer at a time |

Collection distinguishes a device error from a reset from a media change,
because a filesystem must treat them differently: a device error is about one
request, a reset ends everything in flight, and a media change invalidates
cached state. A reset the service asks for terminates its in-flight requests
with a status it can still collect, so a reset cannot leave it waiting for a
completion the device will never send.

The service waits on the completion endpoint it was granted rather than
polling, and the wait carries a deadline. Three rules came out of making that
work:

- **Arm before submitting.** A granted endpoint starts masked, so the
  interrupt source is disabled until its first arm. Arming after submission
  leaves a window in which the completion fires into a disabled source and the
  service then waits for an interrupt that already happened.
- **A record must be read and acknowledged before the next arm.** Leaving one
  queued refuses every later arm and strands the service, so a completion
  taken by the fast path still drains the endpoint.
- **The engine clears the transport's state-change notification** once it has
  read the state behind it. The storage interrupt has two causes, a queued
  completion and a state change, and it cannot be acknowledged while either is
  outstanding. The initial media-present notification was never cleared, so
  the first acknowledgement by the endpoint's owner failed and read as a
  device error. Services still learn about media changes through the
  generations in geometry and completions.

A device that never answers is now bounded rather than a hang: the wait
deadline expires, the service resets the device, and the reset ends every
in-flight request with a status it can still collect. The kernel does not
enforce a deadline of its own — the waiter's deadline is the timeout, which
means a service that chooses to wait forever still can.

In a `K1_QUALIFICATION=1` build the initial image receives no block
capabilities: that harness owns every device IRQ source, and a source has
exactly one owner.

## The facade on real hardware

`sw/userspace/storage/src/lease_block.c` implements `AstraBlockBackend` over
the lease, so the synchronous facade filesystems talk to now runs on the
device rather than only on the deterministic memory backend. The initial image
reads its boot sector through `astra_block_read()` — the call a filesystem
makes — instead of driving the syscalls itself.

Transfer memory is claimed once at attach, sized for one maximum transfer.
Allocating per request would fail under exactly the load a service needs to
survive, because transfer memory is hard-capped.

The syscall ABI names its own structures for what they are — `AstraBlockLeaseInfo`,
`astra_block_lease_query`/`_submit`/`_collect` — because the facade already
owned `AstraBlockGeometry` and `astra_block_query`. Both would have been linked
into the same service, and the collision only became visible when they were.

## Arty bring-up backend

The initial Arty backend is a preallocated image under `/data/astra/storage`,
opened by the Astra machine process before it drops privilege. QEMU presents
that image through the same Vesta-compatible block registers and completion
interrupt expected by Axiom. Linux may create, size, copy, and archive the
offline image, but it never services guest path or file requests.

That QEMU model now exists. The machine implements the Vesta block registers
over an image attached with `-drive if=none`, with 512-byte sectors, a
16-sector transfer ceiling, one active transfer, deferred completion through
the storage interrupt, and a reset-raised state change. `emu/qemu/test-block.py`
certifies the transport directly, and with an image attached the unchanged
kernel reports `AstraHost runtime ... OK, media present`, which is the first
time `sw/kernel/block.c` has executed outside host tests. Without an image the
controller is absent and `SYS_ASTRA_HOST` stays clear, so the existing boot
path is unchanged.

This image is a bring-up and regression backend, not the shipping partition
layout. Moving to a dedicated partition changes only QEMU/host block backing;
the Axiom lease, block service, handlers, VFS, and shell remain unchanged.
The existing `/data/astra/share` Samba export remains a deployment path from
the Mac, not implicit access to the Astra namespace.

## Filesystem handlers

All handlers implement one node-oriented contract:

- mount, recover, sync, unmount, and volume information;
- lookup relative to a directory object;
- create/open/close and stable object identity;
- bounded-offset read/write and truncate;
- cookie-based bounded directory enumeration;
- mkdir, unlink/rmdir, atomic rename, and replace;
- timestamps and versioned attributes;
- read-only transition and media-generation failure;
- change sequence and rescan notification.

VFS owns path parsing, `.`/`..`, assign expansion, mount crossing, access
policy, current directories, and process-visible handles. A handler receives
one validated component at a time. Names and message records are bounded; no
filesystem operation allocates without a charged upper bound.

### Names are case-sensitive and byte-exact

Astra's native namespace is case-sensitive. `Makefile` and `makefile` are two
different files. Name comparison is a byte comparison: VFS and handlers must
not case-fold, must not apply Unicode normalization, and must not reorder or
rewrite bytes on create or lookup. A name is stored exactly as supplied and
matched exactly as supplied.

This is a hard rule, not a default. Case-insensitive lookup silently merges
distinct objects, makes rename non-invertible, and makes the same directory
resolve differently depending on which component created a file. Unicode
normalization has the same failure mode with worse debuggability.

The native ext4 profile therefore never sets the `casefold` feature, and the
frozen `mke2fs` invocation states `^casefold` explicitly rather than relying on
its being off by default. `lwext4` has no notion of the feature at all: it is
absent from `EXT4_SUPPORTED_FINCOM`, so a casefolded volume fails
`ext4_fs_check_features` and mount returns `ENOTSUP`. Both halves are measured
in `sw/userspace/storage/lwext4-eval`.

The FAT/exFAT compatibility handler is the exception and cannot be fixed: the
on-disk format is case-preserving but case-insensitive. That volume is foreign,
mounted read-only, and its behaviour is a property of the format being read,
never a precedent for the native namespace.

### Compatibility handler

The existing game/media partition must remain untouched. FAT/exFAT support is
read-only initially and is selected only after a byte-exact image suite covers
the actual card format, long names, Unicode conversion, malformed metadata,
cycles, truncation, and boundary cases. Failure mounts the volume read-only or
rejects it; it never repairs automatically.

### Native writable handler

Astra will not invent an on-disk journal before it can run a battle-tested
implementation through the shared tests. The leading candidate is a constrained
4 KiB-block ext4 profile presented as Astra's native volume by the VFS service.
The reasons are journaling/recovery, extents, indexed directories, metadata
checksums, 64-bit sizes, mature Linux formatting/inspection/fsck tools, and the
ability to validate images independently.

`lwext4` is **adopted**, as of 2026-08-05. It is vendored at
`third_party/lwext4` from upstream
`58bcf89a121b72d4fb66334f1693d3b30e4cb9c5` (2022-09-22, still upstream HEAD),
without the two GPLv2 files and with the three big-endian defect patches
applied in tree. `third_party/lwext4/ASTRA_VENDOR.md` is the vendor record and
is authoritative on what was excluded and why. The port that binds it to Astra
is `sw/userspace/storage/src/ext4_port.c`, and the build profile that
configures it is `sw/userspace/storage/port/include/generated/ext4_config.h`.

The material that follows records how that decision was reached and what
constrains it.

`sw/userspace/storage/lwext4-eval` now measures both of the recorded risks on
2026-08-04 instead of assuming them. Big-endian is not merely untested: lwext4
never derives `CONFIG_BIG_ENDIAN` itself, no upstream build sets it, and on a
big-endian MC68030 the unpatched library aborts on rename and cannot mount an
`mke2fs`-created ext4 volume at all. Three one-line defects account for all
observed failures: a raw `dentry->inode` read in `ext4_create_hardlink`, a
`to_le32()` applied to the one-byte `s_checksum_type`, and the on-disk
little-endian `s_hash_seed` copied into the htree hash state without
conversion. With those patched, a big-endian m68k build populates an
`mke2fs -t ext4 -b 4096 -I 256 -O ^64bit` image that `e2fsck` reports clean and
that Linux mounts with byte-exact content, and it reads images Linux wrote.

The GPLv2 claim was wrong in one respect. `ext4_extent.c` and `ext4_xattr.c`
are the only GPLv2-or-later files; `ext4_journal.c` and everything else are
**BSD-3-Clause**, so journaling does not import GPLv2 code. Earlier revisions
of this document said BSD-2-clause; that was wrong. The notice carries the
non-endorsement third clause, and upstream's own README calls the GPL-free
subset "BSD3". A build with `CONFIG_EXTENTS_ENABLE=0` and
`CONFIG_XATTR_ENABLE=0` omits both GPLv2 files and passes the same big-endian
checks against an `-O ^extent,^ext_attr` volume, at the cost of indirect block
mapping instead of extents.

**The BSD-3 profile is the one Astra ships**, and the two GPLv2 files are not
vendored at all, so extents and extended attributes are not build switches that
could be flipped later without revisiting the import. The cost is on the
record: indirect block mapping, and no on-disk home for POSIX ACLs or security
labels.

Measured costs. MC68030 object text for the shipped profile is 63,934 bytes
with `m68k-elf-gcc` 13 and 73,568 bytes with `m68k-elf-gcc` 16.1, both with
4,652 bytes of BSS — the spread is the toolchain, not the profile, and the
number is only meaningful when quoted with its compiler. The whole filesystem
stack — lwext4, the port, the block facade, the bounded allocator and the
runtime — links to 82,936 bytes of MC68030 text.

Allocation shape, measured on big-endian MC68030 under `qemu-m68k` against a
16 MiB volume through the shipped port: 9,394 allocations, 888 simultaneously
live blocks, a 126,400-byte peak charge, nothing live at unmount. It is not a
heap workload: 855 live 33..64-byte descriptors and exactly
`CONFIG_BLOCK_DEV_CACHE_SIZE + 1` block buffers.

`sw/userspace/alloc` is the bounded allocator built from that measurement and
described in `USERSPACE_RUNTIME.md`. It carries the workload with zero
failures, zero rejections, zero live blocks at unmount, and its free-list,
bitmap and counter invariants intact, against a 151,936-byte arena.
`astra_ext4_alloc_classes` is the class table, and it is measured on LP32 only.
An LP64 host running the same code produces a different shape — pointer-bearing
structures grow, the 33..64-byte descriptors spill into the next class up, and
the htree sort array grows from 4,092 bytes to 5,456 and no longer fits a 4 KiB
block — so a host measurement must never be used to size it.

The table still has to be re-measured against the real volume size before a
service ships with it, because the journal scales with the volume and 16 MiB is
not that volume.

lwext4 needs no C library, and this is now checked by a link rather than
asserted: `make linkcheck` in `sw/userspace/storage` links the whole stack
under the same `-nostdlib` contract a service uses. What it leaves undefined is
`qsort`, the `mem*`/`str*` primitives, `astra_assert_failed`, the allocator and
block entry points, and four 64-bit libgcc helpers — `__ashldi3`, `__lshrdi3`,
`__udivdi3` and `__umoddi3`. `libastrart` supplies `qsort` (heapsort, chosen
because the arrays being sorted come off a volume Astra did not create) and
`strcpy`; `sw/userspace/runtime/freestanding` supplies the four standard
headers the vendored sources include.

Separately, `ext4_mkfs` mis-accounts free blocks when the last block group is
short, on both endians, and the defect is hidden at lwext4's default 1024-byte
block size. Astra formats offline with `mke2fs`, so this is off the first boot
path. `ext4_mkfs.c` is vendored but not in the built set, along with
`ext4_mbr.c`, which has no caller.

The adoption gate, and where each item stands:

| Condition | State |
|---|---|
| license and source-publication policy explicit | **met** — BSD-3-Clause only, GPLv2 files not imported, recorded in `ASTRA_VENDOR.md` |
| every supported feature frozen in an exact mkfs profile | **met** — `-b 4096 -I 256 -O ^64bit,^casefold,^extent,^ext_attr,^metadata_csum_seed`, held identically by the storage and qualification Makefiles |
| the same images pass native host, m68k emulator, Linux mount and `e2fsck` | **met** for host, m68k emulator and `e2fsck`; Linux loop-mount of an image written by the shipped port is not re-run since adoption |
| power cut after every block write/flush transition, recovery passes | **not done** |
| malformed-image fuzzing cannot escape the service | **not done** |
| performance and memory fit the published budgets | **partly** — memory measured and inside the arena; performance not measured on hardware |

The last three are what stands between the current state and a volume Astra
would trust with a user's data.

Littlefs remains a comparison candidate for bounded crash behavior, but its
raw-flash wear-leveling model and reduced Unix metadata are not an automatic
fit for a large SD-backed desktop volume. The qualification data, not the
name, decides the native implementation.

## Testing and performance

Every retained block and filesystem operation reports:

- calls, success/failure class, bytes or entries;
- total and maximum service ticks;
- queue wait, backend wait, and CPU processing ticks where separable;
- copies, block requests, cache hits/misses, journal transactions, and flushes;
- allocation count/bytes and peak temporary memory;
- m68k cycles and end-to-end wall time.

Required suites are:

1. deterministic unit and state-machine tests;
2. model-based random create/read/write/rename/unlink/enumerate tests;
3. allocation, I/O, timeout, reset, and media-change injection at every site;
4. crash/restart after every persistence transition;
5. malformed and adversarial image corpus plus fuzzing;
6. parallel client and queue-saturation tests;
7. host, sanitizer, static analyzer, m68k emulator, and physical Arty runs;
8. long-run leak and monotonic-growth checks;
9. independent offline verification after every persistent stress run.

Performance baselines are versioned evidence. A new implementation fails the
gate if it silently regresses average or tail latency, throughput, m68k cycles,
copies, or memory. Threshold changes require a recorded explanation rather
than updating expected numbers to match a regression.

The first memory-backend baseline on Beast performs 100,000 deterministic
random operations over 2 MiB and verifies 847,243 sectors against an
independent oracle. The final uninstrumented checkpoint measured 84 ns average
read and 74 ns average write facade cost. The m68k block library is 1,492 bytes
of text and zero data/BSS. Real backend and m68k measurements are
still missing and must not be inferred from this host-memory result.

## Physical input checkpoint

On 2026-08-04 Arty Linux at `192.168.1.188` enumerated the Action Star USB hub
and a Logitech USB Trackball. The trackball is healthy at `/dev/input/event0`
and `/dev/input/by-id/usb-Logitech_USB_Trackball-event-mouse`. The kernel has
USB HID, generic HID, keyboard input, and evdev enabled.

No keyboard evdev node exists. The two Action Star hub HID functions report
`device has no listeners, quitting`; no separate USB keyboard device appears
in `lsusb` or `/proc/bus/input/devices`. Astra cannot consume a keyboard until
Linux enumerates one. This is the current physical gate, not an assumed node
name or an Astra keymap failure.
