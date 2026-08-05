# Astra 68 — Handover: userspace bring-up toward a terminal

Date: 2026-08-04. Written to be read cold in a fresh session.

Goal of this workstream: get far enough to run a shell on Astra, with an
lwext4-backed filesystem underneath it, and continue bootstrapping from there.
`docs/CURRENT_STATE.md` remains the project-wide continuation map; this file is
the resume point for the userspace/storage/loader line of work.

---

## 1. Read this first: nothing is committed

Every change described here is in the working tree and **not committed**. That
includes new files. `git status` should show roughly 17 modified and 15
untracked paths. Do not clean, stash, or reset before reading section 3.

The remote and scratch state described in section 3 is also disposable and
partly expensive to rebuild. It is not required to resume, but knowing what is
already built there saves rebuilding it.

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

Gate status at handover: 30 kernel suites pass, kernel cross-builds, 5 userspace
suites pass under ASan/UBSan, GCC `-fanalyzer` clean, both QEMU certifiers pass.

Not done: nothing loads an ELF at boot yet. No terminal. No VFS. lwext4 is
qualified but **not vendored and not adopted**.

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

The loader works but **nothing loads an ELF at boot**. The immediate next slice
is the one named at the end of `docs/USERSPACE_RUNTIME.md`:

> one runtime-linked service that reads its startup block, queries its ABI
> through it, and exits cleanly.

Concretely:

1. Link a minimal service with `libastrart` + `astra_user.ld` and confirm
   `kernel_elf_accept()` takes it (the test already has an
   `ASTRA_ELF_FIXTURE` hook for exactly this).
2. Get that image into the kernel's reach at boot. `sw/kernel/user_test.S`
   currently supplies flat blobs; the ELF path needs a firmware-supplied image
   instead, per the boot contract.
3. Launch it with `kernel_process_create_executable()` and have it call
   `QUERY_ABI` and `PROCESS_INFO` on its own handle, then exit with a known
   status. That closes the loop end to end and gives the first real
   `PROC:`-shaped data from a real process.

After that, phase 2: the six block admission requirements already specified in
`docs/STORAGE_AND_VFS.md`. The kernel block engine works and is now exercisable
in emulation, so that work is unblocked.

---

## 7. Known problems not caused by this work

- `sw/boot/astra_boot.bin` in the tree is a stale 2026-07-18 build and panics
  with `Vesta timer interrupt timeout` on both the old and the new QEMU. A ROM
  rebuilt from the current tree boots fine. Rebuild or drop the committed
  binary.
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
