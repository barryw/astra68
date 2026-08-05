# Astra 68 — Handover: userspace bring-up toward a terminal

Date: 2026-08-05. Written to be read cold in a fresh session.

Goal of this workstream: get far enough to run a shell on Astra, with an
lwext4-backed filesystem underneath it, and continue bootstrapping from there.
`docs/CURRENT_STATE.md` remains the project-wide continuation map; this file is
the resume point for the userspace/storage/loader line of work.

---

## 1. Read this first

Everything described here is committed. The remote and scratch state in
section 3 is disposable and partly expensive to rebuild; it is not required to
resume, but knowing what is already built there saves rebuilding it.

---

## 2. Current status (one glance)

Working and gated:

| Piece | Location | State |
|---|---|---|
| lwext4 big-endian qualification | `sw/userspace/storage/lwext4-eval` | 3 upstream defects found and patched; e2fsck-clean, Linux-mountable images written from MC68030 |
| Bounded allocator | `sw/userspace/alloc` | 1,270 B MC68030 text; carries lwext4's full workload with zero failures |
| Observability contract | `sw/include/astra/metrics.h`, `sw/userspace/metrics` | registry 320 B text / 388 B BSS; two real modules publish through it |
| QEMU Vesta block service | `emu/qemu/qemu-9.2/hw/m68k/astra68.c` | certified by `emu/qemu/test-block.py`; kernel reports `AstraHost runtime ... OK, media present` |
| ELF acceptance profile | `sw/kernel/elf.c`, `sw/kernel/elf.h` | 1,668 B text; mutation-tested; validated against real toolchain output |
| User link contract | `sw/userspace/runtime/astra_user.ld` | produces exactly the accepted shape |
| Executable loader | `kernel_process_create_executable()` in `sw/kernel/process.c` | transactional; allocation-failure sweep restores exact baseline |
| Process query ABI | syscall 37 `PROCESS_INFO` | capability-gated, tested end to end |
| Initial user image path | boot ABI 0.3, `sw/boot/user_blob.S`, `start_initial_user_image()` | firmware embeds, decodes, CRC-verifies, and describes one image; kernel loads and launches it |
| ROM payload compression | `sw/boot/pack_payload.py`, `decode_payload()`, `sw/common/crc32.c` | kernel and user image ship LZ4; ROM 72.1% used, 73,080 B free |
| First service | `sw/userspace/supervisor` | 1,306 B MC68030 text; verifies startup block, ABI, and its own `PROCESS_INFO` from user mode, then exits with a tagged status |

Gate status at handover: 30 kernel suites pass, kernel cross-builds, 6 userspace
suites pass under ASan/UBSan, GCC `-fanalyzer` clean, both QEMU certifiers pass,
and a full QEMU boot reports:

```
astra68.rom: 189064 payload bytes, 73080 bytes free of 262144 (72.1% used)
  Kernel image ...... OK
Initial image ....... loaded, 6468 bytes, process 0x10000012
Initial image ....... OK, startup block and ABI verified from user mode
```

Decode cost is **not yet measured on hardware**. QEMU's cycle counter is TCG
bookkeeping, not 68030 time, so the firmware's `Kernel image ...... OK, N
cycles` line is meaningless there. Measure it on the board before treating the
compression as free.

Not done: no terminal, no VFS, no block admission syscalls. lwext4 is qualified
but **not vendored and not adopted**.

---

## 3. Environment (non-obvious, saves hours)

Hosts follow `AGENTS.md`: build and test on `beast`; the Mac has
Homebrew `m68k-elf-gcc` and is fine for the ELF and userspace work but
**cannot** build `test_process` (mach-o section attributes) or the kernel image.

Scratch state that exists on Beast at handover time, all disposable:

| Path | What | Rebuild cost |
|---|---|---|
| `/tmp/qemu-m68k-user-build/qemu-m68k` | user-mode qemu-m68k 9.2.4, needed to run the lwext4 probes | ~5 min |
| `/tmp/qemu-final-build/qemu-system-m68k` | system emulator with the block model | ~5 min |
| `/tmp/astra-qemu-final/source-8c7066…` | prepared QEMU source for the above | via `emu/qemu/prepare-source.sh` |
| `/tmp/lwext4-verify/` | lwext4 checkout + eval rig + alloc copy | `git clone` + `make patch` |
| `/tmp/astra-tree/` | rsync of `mk/`, `sw/include`, `sw/userspace`, `sw/kernel` used for all gate runs | rsync |

Two traps that cost time in this session:

- The **astra68 QEMU fork cannot build a `m68k-linux-user` target**:
  `target/m68k/translate.c` registers `INSN(pmmu030, ...)` unconditionally while
  `disas_pmmu030` sits behind `!CONFIG_USER_ONLY`. Guard that line, or build the
  user-mode emulator from an unmodified 9.2.4 tree.
- `qemu-user` installed on Beast is the **armhf** package (for the Arty) and
  cannot execute on x86_64. Do not assume `qemu-m68k` on `PATH` works.

The QEMU tarball is cached at
`/mnt/Documents/astra68/vendor/qemu/qemu-9.2.4.tar.xz`; pass
`ASTRA_QEMU_WORK_ROOT=/tmp/...` to keep prepared sources out of the repo.

---

## 4. Key measured facts and decisions

**lwext4 big-endian is broken upstream, and it is three one-line defects.**
Upstream never sets `CONFIG_BIG_ENDIAN` in any build, so big-endian was never
compiled, let alone tested. Patches are in
`sw/userspace/storage/lwext4-eval/patches/`:

| Site | Defect |
|---|---|
| `ext4.c:1189` | raw `result.dentry->inode` instead of the accessor; aborts rename |
| `ext4_super.c:104` | `to_le32()` applied to the one-byte `s_checksum_type`; every `metadata_csum` volume fails to mount |
| `ext4_hash.c:270` | on-disk little-endian `s_hash_seed` copied into the hash state unconverted; every htree hash wrong |

The third is invisible against lwext4's own `mkfs` (it leaves the seed zero) and
only appears against an `mke2fs` image — which is the profile Astra intends.

**The GPLv2 claim in the old docs was wrong.** `ext4_journal.c` is BSD-2-clause;
only `ext4_extent.c` and `ext4_xattr.c` are GPLv2. A build without those two
passes the same big-endian checks.

**lwext4's own `mkfs` mis-accounts free blocks at 4 KiB** whenever the last
group is short, on both endians, hidden at its default 1024-byte block size. Do
not let Astra format its own volume until that is fixed. Format offline with
`mke2fs`.

**Filesystems are case-sensitive, byte-exact, no Unicode normalization.** The
frozen profile states `^casefold` explicitly. Verified both directions:
`Case.dat`/`case.dat`/`CASE.DAT` coexist and `cAsE.dat` is `ENOENT`; a
`-O casefold` volume is refused at mount with `ENOTSUP` because lwext4 has no
notion of the feature at all.

**Allocation shape is not a heap workload**: 855 live 33..64-byte descriptors
and exactly `CONFIG_BLOCK_DEV_CACHE_SIZE + 1` block buffers. That measurement
is what the allocator's class table was built from, and it must be re-measured
against a real volume size before shipping, because the journal scales.

**Cross-cutting rules are recorded in `docs/OBSERVABILITY.md`** and constrain
every later piece:

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

---

## 5. Where we are in the plan

| # | Piece | State |
|---|---|---|
| 0 | Observability contract | **done** |
| 1 | ELF acceptance, loader, startup block, capability transfer, launch, process query ABI | **done** |
| 1b | Firmware-supplied initial image and the first service that runs from it | **done** |
| 2 | Block admission syscalls | next |
| 3 | Block service + lease-backed `AstraBlockBackend` | |
| 4 | lwext4 vendor + port layer | |
| 5 | VFS service | |
| 6 | Terminal service + VFS client Kit + shell builtins | |
| 7 | Introspection filesystem (`PROC:`) | |

Standing instruction from the user, which shaped all of the above: **no
throwaway code**. Build the real long-term piece even if it is incomplete.
Profile everything so performance is measurable and regressions are visible.

---

## 6. Start here next session

Phase 2: the six block admission requirements already specified in
`docs/STORAGE_AND_VFS.md`. The kernel block engine works, is exercisable in
emulation, and there is now a real process to admit it to.

Facts from the bring-up slice that constrain what comes next:

- **ROM is 72.1% used: 189,064 of 262,144, with 73,080 free.** Both loadable
  images are LZ4 in ROM and CRC-32 verified after decode. The initial image is
  separately capped at 48 KiB decompressed (`ASTRA_USER_IMAGE_MAX_SIZE`); it is
  6,468 bytes today, 1,913 compressed. `docs/MEMORY_MAP.md` holds the budget
  and the rule for what may live in ROM at all.
- **The K1 milestone never completes under QEMU**, with or without media, on
  this tree and on the tree before it. It waits on device qualification the
  emulator does not drive. Do not use milestone output as an emulation gate;
  the initial-image report is printed from the exit path precisely so it does
  not depend on that.
- **The soak build excludes the initial image.** `ASTRA_KERNEL_SOAK_SELFTEST`
  compares an exact free-frame baseline captured before user mode, and the
  image releases frames when it exits.
- **`KERNEL_PROCESS_MAX` is 4** and boot now uses three: survivor, offender,
  and the initial image. Anything that needs a fourth process at boot has to
  raise that limit or retire the K1 qualification pair.
- The supervisor's exit status is tagged (`ASTRA_SUPERVISOR_STATUS_TAG`) so a
  process that never reached user mode — which exits zero — cannot be read as
  success. Keep that property when the service stops exiting and starts
  staying resident: the kernel will need a different liveness signal then.

---

## 7. Known problems not caused by this work

- A stale `sw/boot/astra_boot.bin` may sit in a working tree from an old build
  and panics with `Vesta timer interrupt timeout`. It is **not** committed —
  `git ls-files sw/boot` lists no binaries — so `make` in `sw/boot` replaces it.
  A ROM built from the current tree boots fine.
- `sw/boot` `make test` runs `pytest tests` after the C test. Beast has no
  pytest installed, so that step fails there; the 38 Python tests pass on the
  Mac. Install pytest on Beast or run that half locally.
- Building the ROM now requires the `lz4` command line tool for payload
  compression (Beast has it). Firmware decodes with its own in-tree decoder;
  the dependency is build-time only.
- `sw/userspace/shell/Makefile` passed a header into the compile line, which
  broke `make test` on clang; fixed here, worth knowing if it reappears.
- `docs/MEMORY_BUDGET.md` per-milestone tables are historical and do not carry
  the current `KernelProcess` size. This session moved it 544 → 548 bytes
  (recording the executable span); the compile-time assertion in
  `sw/kernel/process.c` is authoritative and was updated.

---

## 8. How to reproduce the gates

```sh
# userspace: host tests, sanitizers, analyzer, MC68030 cross-build
cd sw/userspace && make test && make sanitize && make analyze && make all

# the whole boot path, including the initial user image (Beast)
cd sw/boot && make astra_boot.bin && make test    # pytest half needs the Mac
timeout 200 /tmp/qemu-final-build/qemu-system-m68k -M astra68 -m 32M \
    -bios astra_boot.bin -nographic -serial mon:stdio \
    -drive if=none,format=raw,file=/tmp/storage.img < /dev/null | \
    grep "Initial image"

# payload verification actually stops a corrupt ROM: flip one byte inside the
# compressed kernel and the firmware must refuse to hand off
python3 -c 'd=bytearray(open("astra_boot.bin","rb").read()); d[0x02fe0+12000]^=0xff;
open("/tmp/corrupt.bin","wb").write(d)'
# expect: POST FAIL: kernel image CRC @ 0x02010000 expected=... actual=...

# kernel: 30 suites plus the cross-built image (Beast only)
cd sw/kernel && make test && make

# ELF profile against a real executable
ASTRA_ELF_FIXTURE=/path/to/service.elf sw/kernel/build/test_elf

# lwext4 big-endian, e2fsck, and the bounded allocator under it
cd sw/userspace/storage/lwext4-eval
export LWEXT4_DIR=/path/to/lwext4 QEMU_M68K=/path/to/qemu-m68k
make patch && make interop && make astra-alloc && make size

# QEMU device models
python3 emu/qemu/test-block.py "$(./emu/qemu/build.sh host)"
python3 emu/qemu/test-input.py "$(./emu/qemu/build.sh host)"

# the kernel driving the block model, with and without an image
qemu-system-m68k -M astra68 -m 32M -bios astra_boot.bin -nographic \
    -drive if=none,format=raw,file=storage.img
```
