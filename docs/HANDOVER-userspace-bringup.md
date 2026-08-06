# Astra 68 — Handover: userspace bring-up toward a terminal

Date: 2026-08-05. Written to be read cold in a fresh session.

Goal of this workstream: get far enough to run a shell on Astra, with an
lwext4-backed filesystem underneath it, and continue bootstrapping from there.
`docs/CURRENT_STATE.md` remains the project-wide continuation map; this file is
the resume point for the userspace/storage/loader line of work.

---

## 1. Read this first

Everything described here is committed. Build and test on `beast`; the Mac has
Homebrew `m68k-elf-gcc` and is fine for the ELF and userspace work but **cannot**
build `test_process` (mach-o section attributes) or the kernel image.

The one-line summary of where this stands: **a real ext4 filesystem now runs on
big-endian MC68030 through the same block facade the ROM-loaded service uses,
`e2fsck` calls the volume it writes clean, and the initial image mounts that
volume from the device lease at every boot.**

The second-transfer defect that blocked the mount is fixed. It was three
distinct faults in the same few lines of `run_request()`, described in section
6, and none of them were in the filesystem.

Under QEMU, with the filesystem-carrying image and a partitioned card:

```
Initial image ....... loaded, 98732 bytes, process 0x10000011, 2 granted capabilities
Initial image ....... startup block and ABI verified from user mode
Initial image ....... block lease and completion endpoint held
Initial image ....... block geometry read
Initial image ....... block round-trip verified, service resident, irq delivered/acked=1/1
Initial image ....... volume found in the partition table
Initial image ....... volume mounted, journal recovered and started
Initial image ....... volume verified, written and re-read, unmounted
```

The last three lines are stages 5, 6 and 7. They mount the volume through the
lease, recover and start the journal, write and re-read a 4 KiB file, unmount,
and then assert nothing is left allocated and the port never addressed outside
its partition window.

Verified on the board as well: it mounts, verifies and unmounts the volume
there, 3 seconds wall from process start, of which roughly 2.8 is POST and
boot.

**Performance was a second, separate defect, and it is fixed too.** See
section 6c. Mount and volume verification went from 82 seconds to 0.10 on
Beast, and from 25.5 seconds to 3 on the board.

Gate status: 30 kernel suites, both kernel build configurations, 8 userspace
suites under ASan/UBSan, GCC `-fanalyzer` clean, the MC68030 kernel image, the
boot C and Python tests, both QEMU device certifiers, repeated QEMU boots with
and without media, and — new in phase 4 — the filesystem mount test on the host
and on big-endian MC68030 under `qemu-m68k` with `e2fsck` as the independent
judge, plus a freestanding link of the whole filesystem stack. The
`ASTRA_ELF_FIXTURE` gate is included and passing again — see 6.5 for why it was
not.

---

## 2. Current status (one glance)

| Piece | Location | State |
|---|---|---|
| Observability contract | `sw/include/astra/metrics.h`, `sw/userspace/metrics` | registry 320 B text / 388 B BSS; real modules publish through it |
| Bounded allocator | `sw/userspace/alloc` | 1,270 B MC68030 text; carries lwext4's full workload with zero failures |
| lwext4 big-endian qualification | `sw/userspace/storage/lwext4-eval` | 3 upstream defects found and patched; e2fsck-clean, Linux-mountable images written from MC68030 |
| QEMU Vesta block + input models | `emu/qemu/qemu-9.2/hw/m68k/astra68.c` | certified by `emu/qemu/test-block.py` and `test-input.py` |
| ELF acceptance profile | `sw/kernel/elf.c` | 1,668 B text; mutation-tested; validated against real toolchain output |
| User link contract | `sw/userspace/runtime/astra_user.ld` | produces exactly the accepted shape |
| Executable loader | `kernel_process_create_executable()` | transactional; grants bootstrap capabilities inside creation |
| Initial user image path | boot ABI 0.4, `sw/boot/user_blob.S`, `start_initial_user_image()` | firmware embeds, decodes, CRC-verifies, and describes one image |
| ROM payload compression | `sw/boot/pack_payload.py`, `decode_payload()`, `sw/common/crc32.c` | kernel and user image ship LZ4, CRC-32 verified after decode |
| Transfer memory | `ASTRA_SYSCALL_DMA_CREATE`, `create_dma_buffer()` | owner-charged, contiguous, cache-inhibited; 4 buffers / 16 pages per service |
| Block admission | `BLOCK_QUERY`/`BLOCK_SUBMIT`/`BLOCK_COLLECT`, ABI `0x0001000a` | lease-gated, fault-injected, round-trip proven at every boot |
| Lease-backed facade | `sw/userspace/storage/src/lease_block.c` | `AstraBlockBackend` on the real device; interrupt-driven with a deadline |
| First service | `sw/userspace/supervisor` | MC68030 text with lwext4 linked in, 98,732-byte ELF, 78 loaded pages; resident; verifies itself, its lease and one real transfer, then mounts the volume |
| lwext4 vendor | `third_party/lwext4` | BSD-3-Clause subset at upstream `58bcf89`; GPLv2 files not imported; 3 big-endian patches applied in tree |
| lwext4 port | `sw/userspace/storage/src/ext4_port.c` | 949 B text; binds `AstraBlockDevice` to `ext4_blockdev`; splits transfers, maps status to errno, refuses a re-entrant lock |
| lwext4 build profile | `sw/userspace/storage/port/include/generated/ext4_config.h` | one profile for host and target; owns errno and oflags so `<errno.h>`, `<fcntl.h>` and `<unistd.h>` drop out |
| Bounded allocation for lwext4 | `sw/userspace/storage/src/ext4_alloc.c` | `astra_ext4_alloc_classes`, measured on LP32; 216,060 B arena, sized by journal size rather than volume size |
| Freestanding C headers | `sw/userspace/runtime/freestanding` | `string.h`, `stdlib.h`, `assert.h`, `inttypes.h`; target only |
| Runtime primitives | `sw/userspace/runtime/src/{sort,assert}.c` | `qsort` (heapsort, 302 B) and a tagged-exit assertion handler (18 B) |
| Initial image ceiling | `sw/include/astra/boot.h`, ABI 0.4 | 256 KiB, was 48 KiB; kernel moved to `0x02044000`; costs no RAM |
| Volume check | `sw/userspace/supervisor/src/volume.c` | mounts through the lease, window-confined; passes at every boot |
| Partition reader | `sw/userspace/storage/src/mbr.c` | read-only MBR; FAT type list identical to `sw/stage0` |

Not done: no VFS and no terminal. The volume mounts, so phase 5 is next.

---

## 3. Environment (non-obvious, saves hours)

Scratch state on Beast, all disposable:

| Path | What | Rebuild cost |
|---|---|---|
| `/tmp/qemu-final-build/qemu-system-m68k` | system emulator with the block and input models | ~5 min |
| `/tmp/qemu-m68k-user-build/qemu-m68k` | user-mode qemu-m68k 9.2.4, needed to run the lwext4 probes | ~5 min |
| `/tmp/astra-qemu-final/source-8c7066…` | prepared QEMU source | via `emu/qemu/prepare-source.sh` |
| `/tmp/lwext4-verify/` | obsolete — lwext4 is vendored now, no external checkout is used | delete it |
| `/tmp/storage.img` | 64 MiB raw image the boot check reads sector 0 from | `truncate -s 64M` |
| `/tmp/astra-qemu-arty/build-arty-*/qemu-system-m68k` | **ARM** emulator for the board, built by `emu/qemu/build.sh arty` | ~6 min |

### The board

`astra-arty`, an Arty Z7-20, at **192.168.1.188**, root ssh from Beast (not
from the Mac). It is a Zynq: ARMv7 Cortex-A9 running Linux 6.6-xilinx, with the
graphics design in the PL. **The m68k is QEMU on the ARM cores, not the FPGA
fabric**, so a board run is real hardware for storage, graphics and the SD path
but is still TCG for the CPU. 68030 timing is no more real there than on Beast;
journal replay cost still cannot be measured this way.

Traps found while deploying:

- **The board's shipped `qemu-system-m68k-astra` predates the block device
  model.** All three binaries under `/data/astra/qemu/bin` return zero for
  `strings … | grep "Astra68 storage image"`. Symptom is
  `AstraHost runtime ... not present` and `0 granted capabilities` even with a
  `-drive` attached. Fix is `emu/qemu/build.sh arty` on Beast, which
  cross-compiles with `arm-linux-gnueabihf-` against armhf pkgconfig.
- The board is BusyBox: no `truncate`, no `timeout`, no `pkill`, and `losetup`
  takes `-o OFS LOOPDEV FILE` rather than `--find`. `/` is read-only; only
  `/data` is writable, so logs go there and not `/tmp`.
- `/data/astra/bin/astra-qemu` requires evdev keyboard *and* mouse and exits if
  either is missing. The board currently has only a trackball, so a headless
  run must invoke the emulator directly with
  `LD_LIBRARY_PATH=/data/astra/qemu/lib`.

Deployed by this work, all non-destructive and alongside the existing files:
`/data/astra/rom/astra_boot-phase4.bin`,
`/data/astra/qemu/bin/qemu-system-m68k-astra-phase4`, and
`/data/astra/storage.img` — 1 GiB, MBR, 64 MiB FAT32 LBA at sector 2048 and an
ext4 volume at sector 133120 on the frozen profile.

Traps that have each cost time:

- The **astra68 QEMU fork cannot build a `m68k-linux-user` target**:
  `target/m68k/translate.c` registers `INSN(pmmu030, ...)` unconditionally while
  `disas_pmmu030` sits behind `!CONFIG_USER_ONLY`. Guard that line, or build the
  user-mode emulator from an unmodified 9.2.4 tree.
- `qemu-user` installed on Beast is the **armhf** package (for the Arty) and
  cannot execute on x86_64. Do not assume `qemu-m68k` on `PATH` works.
- **QEMU's cycle counter is TCG bookkeeping, not 68030 time.** Any `N cycles`
  line read from emulation is meaningless; measure on the board.
- **The K1 milestone never completes under QEMU**, with or without media, and
  did not before this work either. It waits on device qualification the
  emulator does not drive. Do not use milestone output as an emulation gate.

The QEMU tarball is cached at
`/mnt/Documents/astra68/vendor/qemu/qemu-9.2.4.tar.xz`; pass
`ASTRA_QEMU_WORK_ROOT=/tmp/...` to keep prepared sources out of the repo.

---

## 4. Key measured facts and decisions

### 4.1 lwext4

**Big-endian is broken upstream, and it is three one-line defects.** Upstream
never sets `CONFIG_BIG_ENDIAN` in any build, so big-endian was never compiled,
let alone tested. Patches are applied in tree and retained at
`third_party/lwext4/astra/patches/`:

| Site | Defect |
|---|---|
| `ext4.c:1189` | raw `result.dentry->inode` instead of the accessor; aborts rename |
| `ext4_super.c:104` | `to_le32()` applied to the one-byte `s_checksum_type`; every `metadata_csum` volume fails to mount |
| `ext4_hash.c:270` | on-disk little-endian `s_hash_seed` copied into the hash state unconverted; every htree hash wrong |

The third is invisible against lwext4's own `mkfs` (it leaves the seed zero) and
only appears against an `mke2fs` image — which is the profile Astra intends.

**The GPLv2 claim in the old docs was wrong, and so was the correction.**
Only `ext4_extent.c` and `ext4_xattr.c` are GPLv2; everything else, journalling
included, is **BSD-3-Clause**, not BSD-2-clause — the notice carries the
non-endorsement third clause and upstream's README says "BSD3".

Astra ships the BSD-3 subset and **does not vendor the two GPLv2 files at
all**, so extents and xattrs are not switches someone can flip later. Upstream's
root `LICENSE` is the GPLv2 text, which exists only because of those two files;
it is deliberately not imported, because shipping it beside a BSD-3-only subset
would misreport the licence of the whole directory to any audit that reads it.

The price of the exclusion is real: indirect block mapping instead of extents,
and no on-disk home for POSIX ACLs or security labels. Volumes must be
formatted `-O ^extent,^ext_attr`, which is not the default ext4 shape.

**lwext4's own `mkfs` mis-accounts free blocks at 4 KiB** whenever the last
group is short, on both endians, hidden at its default 1024-byte block size. Do
not let Astra format its own volume until that is fixed. Format offline with
`mke2fs`.

**Filesystems are case-sensitive, byte-exact, no Unicode normalization.** The
frozen profile states `^casefold` explicitly. Verified both directions:
`Case.dat`/`case.dat`/`CASE.DAT` coexist and `cAsE.dat` is `ENOENT`; a
`-O casefold` volume is refused at mount with `ENOTSUP`.

**Allocation shape is not a heap workload**: 855 live 33..64-byte descriptors
and exactly `CONFIG_BLOCK_DEV_CACHE_SIZE + 1` block buffers. That measurement is
what the allocator's class table was built from, and it must be re-measured
against a real volume size before shipping, because the journal scales.

**Memory demand is set by the journal size, not the volume size.** Measured on
big-endian MC68030 from 16 MiB to 1 TiB: a journal of 4 MiB or less needs 855
of the 33..64-byte descriptors, 16 MiB or more needs 1,767, and that is flat to
1 TiB. Volume size changes nothing past that step. `mke2fs` derives the journal
from the volume size, which makes the effect masquerade as volume scaling until
measured directly. The frozen profile pins the journal at `-J size=4`, mainly for recovery time
rather than memory: replay after an unclean shutdown scales with outstanding
journal content, and a 128 MiB journal on this hardware bounds worst-case boot
delay 32 times worse than a 4 MiB one. That is unmeasured on hardware and
cannot be measured under QEMU.

The class table is deliberately not shrunk to match: counts 1900/32/4/20, a
216,060-byte arena, sized for the plateau so a card formatted on Linux with
defaults still mounts. `make bigvolume` is that case and does not pin the
journal. Shrinking to 900 would return 64 KiB and refuse such cards.

A 200 GB volume with the default journal used to exhaust the arena — 241
refused allocations, absorbed by lwext4, `e2fsck` clean, and the test reported
PASS. It now fails on any refused allocation.

**The class table is LP32-only.** The identical code on an LP64 host produces a
completely different shape —
class 64 goes *unused* because every pointer-bearing descriptor grows past 64
bytes, and the htree sort array grows from 4,092 bytes to 5,456 and stops
fitting a 4 KiB block at all. The host mount test therefore carries its own
measured table; see `host_classes` in `tests/test_ext4_mount.c`. Never size the
shipped table from a host run.

On MC68030 that sort array is 4,092 bytes against a 4,096-byte class — a
**four-byte margin**. Any increase in the filesystem block size breaks it.

### 4.2 ROM

The ROM is a **fixed 256 KiB window decoded in RTL** (`boot_memory_map.sv`
compares `address[31:18]`). Enlarging it is a bitstream change and therefore a
timing-closure re-qualification, so the budget is fixed and software fits inside
it. `docs/MEMORY_MAP.md` carries the budget table, the measured codec
comparison, and the rule for what may live in ROM at all:

> ROM holds exactly the chain that runs before a filesystem exists, plus enough
> to explain a failure when one never will.

**lwext4 does not belong in ROM.** `sw/stage0` reads FAT in a 2,020-byte image,
so the boot volume is reachable without it and the full filesystem stack loads
as an ordinary file.

Both loadable images ship LZ4 and are CRC-32 verified after decode. That
replaced a read-back comparison compression made impossible, and it preserves
what the comparison actually proved: the destination RAM holds the intended
image. Verified by flipping one byte inside the compressed kernel — it lands in
a literal, decodes cleanly, and only the checksum catches it.

### 4.3 The block path

- **Device rights are not generic rights.** A lease carries `QUERY`,
  `TRANSFER`, and `ADMINISTER` (bits 0, 5, 6). Submitting I/O under
  `ASTRA_RIGHT_WRITE` asks for a right the lease never had and fails as
  `ACCESS_DENIED`.
- **Transfer memory is mapped cache-inhibited.** The 68030 data cache does not
  snoop the device writing into these frames; a cached mapping hands the
  service whichever stale line it read last.
- **Transfer frames are accounted as private mappings.** The VM's alias
  bookkeeping had two classes, private process pages and shared area pages with
  the 68030 alias-class rule. A DMA buffer is mapped exactly once into exactly
  one address space, which is the private case.
- **One buffer carries one transfer at a time.** Filling the 4-deep request
  queue takes four buffers. That is the engine refusing to let two transfers
  share memory it cannot police.
- **Arm the completion endpoint before submitting.** A granted endpoint starts
  masked, so the interrupt source is disabled until its first arm; arming after
  submission leaves a window in which the completion fires into a disabled
  source and the service then waits for an interrupt that already happened.
- **A record must be read and acknowledged before the next arm.** Leaving one
  queued refuses every later arm and strands the service.
- **The storage interrupt has two causes**, a queued completion and a state
  change, and cannot be acknowledged while either is outstanding. The engine
  clears the state change as soon as it has read the state behind it; anything
  else consuming storage interrupts must not assume that bit survives.
- **Completion outcomes are distinct on purpose**: a device error is about one
  request, a reset ends everything in flight, and a media change invalidates
  cached state.

### 4.4 Cross-cutting rules

Recorded in `docs/OBSERVABILITY.md`, and they constrain every later piece:

- metrics are published by sampler callback, never by shared struct layout;
- logging is not a new subsystem — `sw/kernel/trace.c` is already a versioned
  ring, and user records join it so kernel and service events share one order;
- `PROC:` is another VFS handler, not a special mechanism; a handler must be
  able to serve nodes generated at read time with no stable on-disk size;
- **a process-visible handle is a general object handle, not a file
  descriptor** — the one decision that keeps networking from being bolted on;
- reading process state is a rendered view; killing requires process-control
  authority; identifiers are generation-checked so `kill 42` cannot hit a
  recycled process.

Standing instruction from the user, which shaped all of the above: **no
throwaway code**. Build the real long-term piece even if it is incomplete.
Profile everything so performance is measurable and regressions are visible.

---

## 5. Where we are in the plan

| # | Piece | State |
|---|---|---|
| 0 | Observability contract | **done** |
| 1 | ELF acceptance, loader, startup block, capability transfer, launch, process query ABI | **done** |
| 1b | Firmware-supplied initial image and the first service that runs from it | **done** |
| 2 | Block admission syscalls | **done** |
| 3 | Block service + lease-backed `AstraBlockBackend` | **done** |
| 4 | lwext4 vendor + port layer | **done** |
| 4b | Initial image carries the filesystem and runs on the board | **done** (ABI 0.4) |
| 4c | Mount the volume through the device lease | **done** |
| 4d | Terminal on the character plane, changes reaching the disk | **done** |
| 5 | VFS service | next |
| 6 | Terminal service + VFS client Kit + shell builtins | partly, see 6d |
| 7 | Introspection filesystem (`PROC:`) | |

---

## 6. The second-transfer defect, and what it actually was

This section is kept because the three faults it describes are properties of
the block ABI, not of the code that tripped over them, and the next service to
drive a device endpoint will meet all three.

The symptom was that the second transfer on one lease attachment returned
`ASTRA_BLOCK_IO_ERROR`, so `astra_mbr_read()` failed and the supervisor
reported "nothing to mount". Every earlier gate issued exactly one transfer,
and one transfer cannot observe what the endpoint was left in.

All three faults were in `run_request()` in
`sw/userspace/storage/src/lease_block.c`.

### 6.1 Acknowledging re-arms; arming again is refused

`kernel_irq_ack()` re-enables the source and leaves the endpoint `ARMED` when
it takes the last record. `kernel_irq_arm()` accepts only a `MASKED` endpoint
and answers anything else with `KERNEL_IRQ_INVALID_STATE`, which reaches user
mode as `ASTRA_SYSCALL_INVALID_ARGUMENT`.

The service cleared its `armed` flag when it acknowledged, which is backwards,
so the next request armed an already-armed endpoint and was refused before it
reached the device. **The endpoint is armed once per attachment.** The kernel
keeps it armed; nothing needs to arm it again.

### 6.2 Collect before acknowledging, never after an empty collect

`kernel_platform_device_irq_complete()` reports the storage interrupt complete
only when the transport has no queued completion *and* no unread state change.
`BLOCK_COLLECT` is what clears both: it runs `kernel_block_service()` first,
which pops completions and acknowledges the state change.

So an acknowledgement before the collection fails as a device error. That is
not a soft failure — `kernel_irq_ack()` sets `KERNEL_IRQ_EVENT_DEVICE_ERROR`
and masks the endpoint, and no arm undoes it. Only a recover does, and a
recover refuses while a record is still queued.

### 6.3 The completion can land in the gap

Acknowledging straight after a collection that returned nothing is still
wrong, and this is the one that survived the first two fixes. The device
completes the request in the window between the collect returning and the
acknowledgement being issued, and the acknowledgement then reaches a transport
that has a completion queued again. On Beast this lost about one request in
fifteen — thirteen transfers of the mount succeeded before it hit.

The rule that holds: **acknowledge only where nothing is in flight.** That is
two places, and `run_request()` now drains at both and nowhere else:

- at the top, before the arm, which also matters because the kernel refuses to
  arm an endpoint that still holds a record;
- immediately after a collection that returned this request's completion.

Waking from `astra_wait_one()` is *not* such a place. The loop simply collects
again.

`drain()` consumes every queued record rather than one, because the storage
interrupt has two causes and a record left behind refuses every later arm.

### 6.4 What the host test was doing wrong

`tests/test_lease_block.c` passed throughout. Its mock modelled an
acknowledgement as *disarming* the endpoint — the opposite of the kernel — and
had no notion of the transport's completion-valid bit, so every ordering above
was legal in the model.

The mock is now a state machine that mirrors `kernel_irq_arm()`,
`kernel_irq_ack()` and the completion-valid rule, including a mode where the
device answers in the gap after an empty collect. Against the code as it was,
these tests fail. That is the point of them.

### 6.5 Two build defects found on the way

Both cost an experiment each and are fixed:

- **`sw/userspace/supervisor/Makefile` did not relink on a changed library.**
  The archives were order-only prerequisites of a phony `runtime` target, and
  make stats a file it has no rule for *before* running that sub-make, so a
  rebuilt `libastrablock.a` was compared against a stale timestamp and the
  previous ELF stayed in place. It builds in two invocations now. This is the
  stale-object trap in a different hat: the image boots, and it is not the code
  you just wrote.
- **The `ASTRA_ELF_FIXTURE` gate had been failing since lwext4 was linked in.**
  `test_elf.c` measured a real executable against the 64-page ceiling used for
  its hand-built images, while the kernel had already been raised to 128 pages
  for exactly this image. The supervisor needs 78. `KERNEL_PROCESS_IMAGE_PAGES_MAX`
  now lives in `process.h` so the loader and its test cannot hold different
  numbers, and the fixture is measured against the limits the kernel applies.
  The test also lost its failure message to a buffered `stdout` that `abort()`
  never flushed, which is why it looked like a silent crash.

### Reproducing

```sh
cd sw/userspace && make all && cd ../boot && make astra_boot.bin
timeout 240 /tmp/qemu-final-build/qemu-system-m68k -M astra68 -m 32M \
    -bios astra_boot.bin -nographic -monitor none -serial stdio -no-reboot \
    -drive if=none,format=raw,file=/tmp/part.img < /dev/null | grep "Initial image"
```

Eight `Initial image` lines, ending in "volume verified, written and re-read,
unmounted".

`astra_progress()` remains the cheapest instrument in this path: the kernel
prints `stage N` for any value it does not recognise, and the counter is
monotonic, so probe values must increase or they are silently refused.

## 6c. The wait latency, and why the block path was not interrupt-driven

Fixed, but the shape is worth keeping: the measurement pointed at three
innocent parties before it reached the guilty one.

Every transfer that had to block cost a **full lease timeout** -- two seconds.
A mount was 82 seconds on Beast and 22.7 on the board. The same I/O volume
(2,112 reads, 7,374 writes, 4,264 splits) driven straight at a file through
`lwext4-eval` takes **0.08 seconds**, which is what proved neither lwext4 nor
TCG was responsible.

Measured at each layer, and each one exonerated in turn:

| Interval | Latency |
|---|---|
| submit to the transport completing the I/O | under 1 ms, 0 slow events in 49 |
| completion to its interrupt being enabled | `enable=1 level=3` on all 49 |
| interrupt raised to the kernel handler running | 29 to 886 us |
| interrupt raised to the **waiting thread** running | **2.000 s** |

Two seconds is the lease deadline to the microsecond. Cutting the deadline to
100 ms moved the wake to 100.05 ms, which settled it: the wake was never the
interrupt's doing. The thread slept to its own deadline every time.

The cause is that **device interrupts are always deferred**
(`dispatch_device_interrupt()`): the handler queues the event and signals the
worker, so the thread is woken later inside `service_deferred_interrupts()`,
by which point no interrupt is left to carry a scheduling decision. The
interrupt path could not have made one anyway -- an idle kernel is halted in
`kernel_worker_arch_wait()`, so the frame is a supervisor frame and
`interrupt_entry_dispatch_fast()` returns without scheduling.

That left `kernel_process_worker_resume()`, which resumed `current_thread`
rather than selecting one. With nothing running it returned NULL, the worker
halted again, and the next thing to run was the timer -- armed to the sleeping
thread's own deadline. It selects a ready thread now, the way the supervisor
timer path already did.

Lessons that outlive this defect:

- **A deadline that exactly matches an observed latency is not a coincidence.**
  Two seconds was the answer before the cause was known.
- **Instrument both sides of the boundary.** The QEMU-side probes proved the
  device was fast and the interrupt prompt; the guest-side counters proved the
  waits returned OK rather than timing out. Neither alone would have located
  it, and both were needed to stop suspecting the emulator.
- **`stop #0x3000` really does halt**, and the CPU really does wake on the
  interrupt. The handler ran in microseconds. Only the *thread* was late.

## 6b. Where to go next

**Run it on the board.** Everything above is verified on Beast only.

Then phase 5 proper — the VFS service — and the loader it needs. Note that
**the loader is no longer the blocker it was**: ABI 0.4 raised the initial image
ceiling to 256 KiB, so a service can carry a filesystem without being loaded
from a file. The file loader is still where this is going, because phases 6 and
7 load a terminal, a shell and fonts, but it is no longer on the critical path
for proving storage.

Constraints that will bite:

- **The port refuses a re-entrant block-device lock with `EBUSY`** and counts it
  in `reentry_refusals`. A deliberate tripwire: the service is single-threaded
  today, and this fires the day it is not, instead of silently corrupting the
  block cache. Threads mean replacing it with a real lock.
- **The acknowledge-only-when-idle rule in 6.3 assumes one request in flight.**
  The engine allows four. A service that pipelines requests cannot reason
  "nothing is in flight" per request and needs a different rule.
- **The four user LEDs on the Arty are PL pins driven from RTL**, not from the
  PS: `astra_arty_graphics_top.sv` assigns them `video_locked`, `~build_reset`,
  `scene_active` and `frame_counter[5]`, which is the three-on-one-flashing the
  board shows. There is no AXI GPIO and no EMIO routed to them, so nothing in
  software can drive one -- an activity light is a bitstream change and a
  timing-closure re-qualification. The cheapest version, if it is ever worth
  it, repoints `led[3]` at a spare bit of the existing graphics register file
  at `0x43c00000` rather than adding new IP.
- **Transfer splitting is in the port, not in lwext4.** `run_transfer()` chops
  requests to `max_transfer_sectors`. The m68k gate splits 7,555 times.
- **Several device failures collapse to `EIO`** at the lwext4 boundary, because
  lwext4's errno set has no `ETIMEDOUT` or `ECANCELED`. The exact status is in
  `AstraExt4Port::last_status`; anything deciding whether to retry reads it
  there, not the errno.
- **A failed assertion inside lwext4 exits the process** with
  `ASTRA_ASSERT_STATUS_TAG | line`, i.e. `0x4153xxxx`. For the initial image
  that is a kernel panic. A panic status starting `0x4153` means the low
  halfword is a line number in a `third_party/lwext4/src` file.
- **The status halfword is full.** Bit 15 was the last one and the volume check
  took it. Anything else that needs to report failure uses the progress counter.
- **The service is single-threaded and synchronous.** One request in flight even
  though the engine allows four.
- **`KERNEL_PROCESS_MAX` is 4**; the default boot uses one, K1 costs two more.
- **Name collisions are real here.** The syscall ABI uses `AstraBlockLeaseInfo`
  and `astra_block_lease_*` precisely because `AstraBlockGeometry` and
  `astra_block_query` were already taken. Check before naming.
- **The supervisor's exit status is a tagged halfword** and any exit is a boot
  failure the kernel turns into a panic.
- Two things the block path still lacks: a **kernel-enforced per-request
  deadline** (today the waiter's deadline is the timeout), and **`LATE` and
  `CANCELLED` completions** have ABI values but no producer.

## 6d. The terminal, and the dangling device behind it

Astra boots to a terminal on the character plane, on Beast and on the board,
driven by keys injected over QMP `input-send-event`. Working: `ls`, `cd`,
`pwd`, `mkdir`, `cat`, `write`, `rm`, `clear`, `help`, line editing, history,
scrolling. **What the terminal changes now reaches the disk**, judged from
outside by `e2fsck` and `debugfs` on the card the emulator wrote.

### What the defect actually was

The symptom was that nothing the terminal did survived a restart: a `mkdir`
lasted the session and `debugfs` showed only `lost+found`, while `write`
reported `write: short, 0 of 5`.

**It was not lwext4, not big-endian, and not flushing.** It was a lifetime bug
in `sw/userspace/supervisor/src/main.c`. `verify_block_round_trip()` held the
`AstraLeaseBlock` and the `AstraBlockDevice` as **locals**, and:

```c
failure = supervisor_verify_volume(&block, want_terminal);  /* leaves it MOUNTED */
astra_lease_block_detach(&lease);                           /* releases the lease */
return failure;                                             /* frame dies */
```

`console_shell_run()` then ran on that reclaimed stack. A mounted volume keeps
`volume_port.device` pointing at the device, so every filesystem write
dereferenced a dead frame, read a corrupted `max_transfer_sectors` out of it
and was refused as `ASTRA_BLOCK_TRANSFER_TOO_LARGE`. Reads kept working out of
lwext4's block cache, which is exactly why it read as a filesystem defect.

Measured at the moment of a failed `write`: `alloc live=40 peak=46 fail=0
oop=0 last=5 wfail=17 wok=62`. Zero allocator failures, zero out-of-partition
refusals, and 17 of 62 device writes refused with status 5. Nothing about that
is a filesystem.

The fix gives the lease and the device process lifetime and detaches only when
the volume is not mounted, with `supervisor_volume_is_mounted()` as the single
source of truth for that question.

### What was disproved on the way, so it is not re-investigated

- **Partial-block writes are fine on both endians.** 1, 5, 4095, 4096 and 4097
  bytes through the port all move their full byte count, on the LP64 host and
  on big-endian MC68030 under `qemu-m68k`. The "moved zero bytes" reading was
  the dangling device, not a read-modify-write defect.
- **The unmount was never the flush.** A volume written and abandoned without
  unmounting comes back complete after `ext4_recover()`, on both endians.
- **`ext4_cache_write_back()` is not the difference.** The passing gates enable
  it and the supervisor does not; forcing it on changes nothing here.
- **The LP64 class table on an LP32 target starves lwext4 silently.** A probe
  that used `host_classes` on m68k lost every transaction after the second and
  looked exactly like a big-endian journal defect. `sizeof(void *) == 4u`
  selects `astra_ext4_alloc_classes`; the mount test does this and any new
  probe must too. This cost an hour and will cost the next person the same.

### Flushing, as measured

- **Per-operation journal commit happens.** Every mutating lwext4 call is
  wrapped in `ext4_trans_start`/`ext4_trans_stop`, and `trans_stop` runs
  `jbd_journal_commit_trans`. A command is durable at the journal when it
  returns.
- **Checkpointing to final locations is lazy, and that is correct.** After a
  killed emulator, `debugfs` without replay sees the dirents but zero-size
  inodes; `e2fsck` with replay completes it and a second pass is clean.
- **There is no unmount on shutdown**, so the superblock free-block and
  free-inode counters are stale after every hard stop. `e2fsck` corrects them.
  The volume is correct; it is merely always dirty.
- **`astra_block_flush()` has no caller anywhere in `sw/`.** lwext4's
  `ext4_blockdev_iface` has no flush hook, so the port never issues one and
  there is no barrier between a journal commit and the media. Harmless under
  QEMU. Not harmless on the board's real SD path, and it is the next thing to
  fix in this area.

Three related notes:

- **A second mount is not possible.** lwext4 answers EINVAL from inside
  `ext4_block_init`/`ext4_fs_init`, not from the mount preamble, and
  re-initialising the port first does not help. So "mount once and keep it" is
  the only shape available, which is why the check takes a `keep_mounted` flag
  — and it is that flag that created the lifetime bug above.
- **Nothing prevents unmounting a volume in use**, here or in lwext4. The VFS
  service needs open-handle refcounting and an `EBUSY` on unmount, and it is
  cheaper to build that in than to retrofit it.
- **The terminal is not visible on the board's monitor.** Its bytes are in the
  character plane -- a read of `0xFFF22000` returns the prompt and the `ls`
  output -- but the screen still shows the Zynq-side splash, because the raw
  emulator was invoked instead of `/data/astra/bin/astra-qemu` and nothing
  handed scanout over. Astra wants an explicit notion of a text display and a
  graphics display, with the terminal claiming the text one and clearing the
  splash behind it, rather than relying on whatever the launcher happened to
  leave on screen. **This is the only reason a terminal has not been seen on a
  screen; it runs.**

## 6e. What the boot path costs, and what still cannot be seen

`emu/qemu/time-boot.py` times each stage from the serial stream. Baseline on
Beast, 5 runs, median seconds since launch:

| Stage | Reached | Delta |
|---|---|---|
| POST | 0.02 | 0.02 |
| kernel handoff | 0.02 | 0.00 |
| user image loaded | 0.05 | 0.02 |
| block round-trip | 0.05 | 0.01 |
| partition read | 0.05 | 0.00 |
| mount + journal | 0.08 | 0.02 |
| volume verify | 0.09 | 0.02 |
| terminal up | 0.09 | 0.00 |

Spread across runs is 0.08 to 0.10, so a stage that doubles is visible. `--budget`
turns it into a gate. **These are host seconds for a fixed workload, not 68030
time**, and the same is true on the board: the CPU is TCG in both places.

The filesystem workload is deterministic and worth diffing directly: `make
ext4-test` reports 2,112 reads / 7,374 writes / 4,264 splits every run. The
splits are the test forcing `max_transfer_sectors` to 4; the device model
reports 16, and a 4 KiB block is 8 sectors, so the real path splits nothing and
costs one device round trip per block.

**The metrics registry is wired to nothing.** `sw/userspace/metrics` implements
the sampler contract from `docs/OBSERVABILITY.md` and is covered by its own
test, `astra_block_sampler` and `astra_alloc_sampler` both exist, and no shipped
code calls `astra_metric_register` — the supervisor does not even link the
module. `--gc-sections` collects all three. So the numbers above come from
outside the machine, and nothing running on Astra can report its own. That is
not costly to fix, but it needs a reader to be worth anything, and the reader is
`PROC:` in phase 7.

## 7. Known problems not caused by this work

- **`KERNEL_DEVICE_LEASE_OWNER_MAX` was raised from 2 to 4 for the terminal's
  display lease and `sw/kernel/tests/test_device.c` was not updated**, so
  `make test` in `sw/kernel` aborted in `test_quota_and_owner_death` from
  commit `46923c7` onward. The test now derives its counts from the constant
  rather than writing them out, so the next bump cannot repeat it.
- A stale `sw/boot/astra_boot.bin` may sit in a working tree from an old build
  and panics with `Vesta timer interrupt timeout`. It is **not** committed —
  `git ls-files sw/boot` lists no binaries — so `make` in `sw/boot` replaces it.
- `sw/boot` `make test` runs `pytest tests` after the C test. Beast has no
  pytest installed, so that step fails there; the 38 Python tests pass on the
  Mac. Install pytest on Beast or run that half locally.
- Building the ROM requires the `lz4` command line tool for payload compression
  (Beast has it). Firmware decodes with its own in-tree decoder; the dependency
  is build-time only.
- **Stale objects are indistinguishable from kernel bugs.** The userspace
  Makefiles now generate and include depfiles, because a runtime object built
  against an older ABI constant boots and then fails its startup check with
  exit 127. If you ever see 127, suspect a stale object first.
- **`mke2fs` defaults move, and the frozen profile is subtractive.** A recent
  `mke2fs` enables `metadata_csum_seed`, which lwext4's supported-incompat set
  does not contain, so mount returns `ENOTSUP` (95). The profile subtracts it
  explicitly. If a future `mke2fs` adds another default incompat feature the
  symptom is the same `ENOTSUP` at mount, and the fix is another `^feature` in
  `EXT4_MKFS_FEATURES` in both `sw/userspace/storage/Makefile` and the
  qualification rig's Makefile, which deliberately hold identical copies.
- **On macOS the `mke2fs` on `PATH` may be Android platform-tools'**, and there
  is no `e2fsck` or `dumpe2fs` beside it. It formats well enough for
  `make ext4-test`, but the `e2fsck` half of the gate only exists on Beast.
- The **`-DASTRA_FORCE_LE` control build is expected to fail to mount** a real
  image with `ENOTSUP`. That failure is the control working, not a regression.
- lwext4's config guard is spelled `CONFIG_USE_DEFAULT_CFG`, not
  `CONFIG_USE_DEFAULT_CONFIG`. The old eval rig defined the latter and worked
  anyway, because `#if !CONFIG_USE_DEFAULT_CFG` reads an undefined macro as 0
  and includes the profile regardless.
- `docs/MEMORY_BUDGET.md` per-milestone tables are historical. `KernelProcess`
  is now 596 bytes (472 originally, +4 for the executable span, +48 for the
  four transfer-memory records); the compile-time assertion in
  `sw/kernel/process.c` is authoritative.

---

## 8. How to reproduce the gates

```sh
# userspace: host tests, sanitizers, analyzer, MC68030 cross-build
cd sw/userspace && make test && make sanitize && make analyze && make all

# kernel: 30 suites, the default image, and the qualification image
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1

# the whole boot path, including the initial user image (Beast)
cd sw/boot && make astra_boot.bin && make test    # pytest half needs the Mac
truncate -s 64M /tmp/storage.img
timeout 200 /tmp/qemu-final-build/qemu-system-m68k -M astra68 -m 32M \
    -bios astra_boot.bin -nographic -serial mon:stdio \
    -drive if=none,format=raw,file=/tmp/storage.img < /dev/null | \
    grep "Initial image"
# expect the five Initial image lines from section 1, ending in
# "block round-trip verified, service resident, irq delivered/acked=1/1"

# without media the service must still come up and park
timeout 100 /tmp/qemu-final-build/qemu-system-m68k -M astra68 -m 32M \
    -bios astra_boot.bin -nographic -serial mon:stdio < /dev/null | \
    grep "Initial image"

# payload verification actually stops a corrupt ROM: flip one byte inside the
# compressed kernel and the firmware must refuse to hand off
python3 -c 'd=bytearray(open("astra_boot.bin","rb").read()); d[0x02fe0+12000]^=0xff;
open("/tmp/corrupt.bin","wb").write(d)'
# expect: POST FAIL: kernel image CRC @ 0x02010000 expected=... actual=...

# ELF profile against a real executable
ASTRA_ELF_FIXTURE=sw/userspace/supervisor/build/m68k/astra_supervisor.elf \
    sw/kernel/build/test_elf

# the filesystem on the host, and the freestanding link of the whole stack
cd sw/userspace/storage
make ext4-test      # needs mke2fs; builds a fresh volume every run
make linkcheck      # proves lwext4 + port + allocator need no C library

# the filesystem on big-endian MC68030, judged by e2fsck (Beast)
cd sw/userspace/storage/lwext4-eval
export QEMU_M68K=/tmp/qemu-m68k-user-build/qemu-m68k
make interop        # populate under qemu-m68k, then e2fsck -fn
make reread         # a second process re-mounts and re-verifies what it wrote
make partitioned    # a real card layout; fails if the boot region moved
make measure        # the LP32 allocator class measurement
make bigvolume      # a 200 GB sparse volume; BIG_GB= to change the size
make size

# QEMU device models
python3 emu/qemu/test-block.py "$(./emu/qemu/build.sh host)"
python3 emu/qemu/test-input.py "$(./emu/qemu/build.sh host)"

# what the boot path costs, per stage, so a regression in it is visible
python3 emu/qemu/time-boot.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --runs 5 --budget 1.0

# the partitioned boot: eight Initial image lines, ending in the mounted volume
timeout 240 /tmp/qemu-final-build/qemu-system-m68k -M astra68 -m 32M \
    -bios sw/boot/astra_boot.bin -nographic -monitor none -serial stdio \
    -no-reboot -drive if=none,format=raw,file=/tmp/part.img < /dev/null |
    grep "Initial image"

# the board (from beast; see docs/INVENTORY.md for the layout it expects)
emu/qemu/build.sh arty          # the emulator must carry the storage model
scp sw/boot/astra_boot.bin root@192.168.1.188:/data/astra/rom/astra_boot-fs.bin
ssh root@192.168.1.188 'LD_LIBRARY_PATH=/data/astra/qemu/lib \
    /data/astra/qemu/bin/qemu-system-m68k-astra-phase4 -M astra68 -m 32M \
    -bios /data/astra/rom/astra_boot-fs.bin -nographic -monitor none \
    -serial stdio -no-reboot \
    -drive if=none,format=raw,file=/data/astra/storage.img'
```
