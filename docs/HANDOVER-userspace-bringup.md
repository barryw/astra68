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
and `e2fsck` calls the volume it writes clean.**

Before that, and still true: a user-mode service loaded from ROM reads sectors
off a real block device through that facade, and the kernel proves it on every
boot.

```
astra68.rom: 191864 payload bytes, 70280 bytes free of 262144 (73.2% used)
Initial image ....... loaded, 15140 bytes, process 0x10000011, 2 granted capabilities
Initial image ....... startup block and ABI verified from user mode
Initial image ....... block lease and completion endpoint held
Initial image ....... block geometry read
Initial image ....... block round-trip verified, service resident, irq delivered/acked=1/1
```

Gate status: 30 kernel suites, both kernel build configurations, 8 userspace
suites under ASan/UBSan, GCC `-fanalyzer` clean, the MC68030 kernel image, the
boot C and Python tests, both QEMU device certifiers, repeated QEMU boots with
and without media, and — new in phase 4 — the filesystem mount test on the host
and on big-endian MC68030 under `qemu-m68k` with `e2fsck` as the independent
judge, plus a freestanding link of the whole filesystem stack.

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
| Initial user image path | boot ABI 0.3, `sw/boot/user_blob.S`, `start_initial_user_image()` | firmware embeds, decodes, CRC-verifies, and describes one image |
| ROM payload compression | `sw/boot/pack_payload.py`, `decode_payload()`, `sw/common/crc32.c` | kernel and user image ship LZ4, CRC-32 verified after decode |
| Transfer memory | `ASTRA_SYSCALL_DMA_CREATE`, `create_dma_buffer()` | owner-charged, contiguous, cache-inhibited; 4 buffers / 16 pages per service |
| Block admission | `BLOCK_QUERY`/`BLOCK_SUBMIT`/`BLOCK_COLLECT`, ABI `0x0001000a` | lease-gated, fault-injected, round-trip proven at every boot |
| Lease-backed facade | `sw/userspace/storage/src/lease_block.c` | `AstraBlockBackend` on the real device; interrupt-driven with a deadline |
| First service | `sw/userspace/supervisor` | 5,514 B MC68030 text; resident; verifies itself, its lease, and one real transfer |
| lwext4 vendor | `third_party/lwext4` | BSD-3-Clause subset at upstream `58bcf89`; GPLv2 files not imported; 3 big-endian patches applied in tree |
| lwext4 port | `sw/userspace/storage/src/ext4_port.c` | 949 B text; binds `AstraBlockDevice` to `ext4_blockdev`; splits transfers, maps status to errno, refuses a re-entrant lock |
| lwext4 build profile | `sw/userspace/storage/port/include/generated/ext4_config.h` | one profile for host and target; owns errno and oflags so `<errno.h>`, `<fcntl.h>` and `<unistd.h>` drop out |
| Bounded allocation for lwext4 | `sw/userspace/storage/src/ext4_alloc.c` | `astra_ext4_alloc_classes`, measured on LP32; 216,060 B arena, sized by journal size rather than volume size |
| Freestanding C headers | `sw/userspace/runtime/freestanding` | `string.h`, `stdlib.h`, `assert.h`, `inttypes.h`; target only |
| Runtime primitives | `sw/userspace/runtime/src/{sort,assert}.c` | `qsort` (heapsort, 302 B) and a tagged-exit assertion handler (18 B) |

Not done: no VFS, no terminal. The filesystem runs, but nothing above it does,
and it has never run against the real device — see section 6.

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
| 5 | VFS service | next |
| 6 | Terminal service + VFS client Kit + shell builtins | |
| 7 | Introspection filesystem (`PROC:`) | |

---

## 6. Start here next session

Phase 5: the VFS service.

Read first: `third_party/lwext4/ASTRA_VENDOR.md` for what was imported and why,
and `sw/userspace/storage/include/astra/ext4_port.h`, whose header comment
states the three translations the port owns.

**What phase 4 did not do, and what it means.** The filesystem has only ever
run against the *memory* backend. Every gate — host and m68k — mounts a volume
held in RAM. It has never been mounted through `lease_block.c` on the real
device, and that is not an oversight: there is no way to get there yet. lwext4
is 64–74 KiB of text and `docs/MEMORY_MAP.md` rules it out of the 256 KiB ROM
window, so the filesystem must load as an ordinary file — and the loader that
would read that file is the thing phase 5 builds. The facade is the same object
in both cases, which is the entire argument for believing the first real-device
mount will work, but it is an argument and not yet a measurement.

The first thing worth doing in phase 5 is therefore probably not the VFS
surface. It is closing that loop: get lwext4 onto the real device once, however
crudely, so the port is proven against the transport it will actually use
before a service is designed on top of it.

Constraints that will bite:

- **The port refuses a re-entrant block-device lock with `EBUSY`** and counts
  it in `reentry_refusals`. That is a deliberate tripwire, not a limitation:
  the service is single-threaded today, and the day it is not, this fires
  instead of silently corrupting the block cache. If phase 5 adds threads, this
  is the first thing that must be replaced with a real lock.
- **Transfer splitting is in the port, not in lwext4.** lwext4 asks for as many
  blocks as it likes; `run_transfer()` chops the request to the device's
  `max_transfer_sectors`. The m68k gate splits 4,264 times, so this path is
  well exercised, but it means a device reporting a tiny cap silently costs
  many more transfers rather than failing.
- **Several device failures collapse to `EIO`** at the lwext4 boundary because
  lwext4's own errno set has no `ETIMEDOUT` or `ECANCELED`. The exact status
  survives in `AstraExt4Port::last_status`, and anything deciding whether to
  retry must read it there, not the errno.
- **A failed assertion inside lwext4 exits the process** with
  `ASTRA_ASSERT_STATUS_TAG | line`, i.e. `0x4153xxxx`. For the initial image
  that is a kernel panic. If a panic status starts with `0x4153`, the low
  halfword is a line number in a `third_party/lwext4/src` file.
- **The service is single-threaded and synchronous.** One request is in flight
  at a time even though the engine allows four. A filesystem worker blocking on
  a transfer blocks the whole service until phase 5 gives it threads.
- **A filesystem wanting concurrent I/O needs a buffer per outstanding
  request**, and transfer memory is capped at 4 buffers and 16 pages per
  service.
- **`KERNEL_PROCESS_MAX` is 4**; the default boot uses one, and K1 costs two
  more when enabled.

- **Name collisions are real here.** The syscall ABI and the storage facade are
  linked into the same program; the ABI uses `AstraBlockLeaseInfo` and
  `astra_block_lease_*` precisely because `AstraBlockGeometry` and
  `astra_block_query` were already taken. Check before naming.
- **The supervisor's exit status is a tagged halfword**, not a byte, and any
  exit is a boot failure the kernel turns into a panic.
- Two things the block path still lacks: a **kernel-enforced per-request
  deadline** (today the waiter's deadline is the timeout, so a service that
  chooses to wait forever still can), and **`LATE` and `CANCELLED` completions**
  have ABI values but no producer.

---

## 7. Known problems not caused by this work

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
```
