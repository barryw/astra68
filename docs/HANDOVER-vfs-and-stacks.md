# Astra 68 — Handover: the storage protocol, and thread stacks

Date: 2026-08-06. Written to be read cold in a fresh session.

This is the resume point for two things: making the system **pluggable** through
typed service protocols, and giving a thread a **usable stack**. They arrived
together because the first one is what exposed the second.

`docs/HANDOVER-userspace-bringup.md` remains the resume point for the
storage/loader line beneath this. `docs/CURRENT_STATE.md` is the project-wide
map; mind its override. Where this file and an older one disagree, this one is
newer.

---

## 1. Where things stand

Nothing here is pushed. `main` is at `8b24f0a`, which is where the terminal
persistence fix landed earlier in the same session.

**Branch `review/kernel-storage-hardening`** — 8 commits, tree clean, every gate
green. Ready to merge.

| Commit | What |
|---|---|
| `d751509` | `--gc-sections` everywhere; two dead kernel exports removed |
| `2eea811` | raised kernel warning gate, and the four sites it found |
| `3dd1ded` | lwext4 patch 0004: `ext4_fwrite` reporting failed writes as success |
| `3e8f792` | MBR reader refuses overlapping partitions |
| `34d6638` | port syscalls lifted out of the dispatch switch |
| `fa9c4f9` | boot-path timing harness and baseline |
| `79becbd` | the storage protocol and the service behind it |
| `27d8016` | three-page thread stacks |

**Branch `wip/vfs-terminal-wiring`** — 1 commit, **does not work**, do not merge.
It is the wiring that makes the terminal a client of the protocol. Kept on a
branch so it is not lost. See section 4.

---

## 2. Do these next, in this order

### 2.1 Diagnose the terminal hang

`wip/vfs-terminal-wiring` moves the shell onto the storage protocol. With
three-page stacks it no longer faults, and all four typed commands are accepted
over QMP, but **the shell stops mid-prompt after the first Enter and nothing
reaches the disk**. A hang, not a fault; it survives a 60-second settle.

The lead: the prompt renders exactly two characters and stops. That points at
the terminal flush path rather than at the filesystem. The service core, the
client Kit, the lwext4 backend and the local transport all pass their own tests
on `review/kernel-storage-hardening`, so only the wiring is suspect.

What is already ruled out, so it is not re-derived:

- **Not the fault this replaced.** At one page the chain overflowed at
  `0x6FFFFFEC`, twenty bytes below the `0x70000000` stack base. That is fixed.
- **Not the Kit's stack cost.** The client's in-flight records live in the
  client, not on the stack; its frames are 24 bytes.
- **Not `console_shell_run`'s frame.** Moving the input batch out of it took it
  from 1,624 bytes to 84. GCC then inlines the commands into `run_line`, whose
  frame is 1,260, and that is where the remaining stack goes.

### 2.2 Reserve-and-grow stacks

Three pages is the interim shape and `sw/kernel/thread.h` says so where the
constants are defined. The intended shape:

- keep `KERNEL_THREAD_STACK_STRIDE` as a **reservation** and widen it;
- commit one page at thread creation, as before;
- on a user fault, if the address is inside the reservation and above the guard
  floor, map a page and resume instead of killing the process;
- keep the floor page unmapped, so a genuine wild pointer still dies.

**The blocker is the user-fault path.** Today every user fault sets
`KERNEL_PROCESS_EXIT_USER_FAULT` and kills the process — there is no demand
paging anywhere in the kernel, checked. Nothing asks whether a fault was a
stack growing.

Nothing has to be unpicked first: the size becomes the initial commit and the
stride becomes the cap.

Why this shape rather than the Amiga's `stack` command: reserving address space
costs page-table entries, not RAM, so the common case needs no number at all,
and the guard page turns a wrong guess from silent corruption into a clean
fault. Linux does the same for a main thread; Windows carries reserve and commit
in the PE header; Go copies growable stacks, which needs compiler cooperation
Astra does not have.

---

## 3. The storage protocol

Astra's `docs/DRIVER_AND_SERVICE_ARCHITECTURE.md` had said since it was written
that filesystem handlers run in protected userspace behind typed protocols.
Nothing implemented it: the supervisor was the registrar, the block service, the
filesystem and the terminal in one process, and the shell reached lwext4 through
nineteen direct calls.

### 3.1 What exists now

| Piece | Where | m68k text |
|---|---|---|
| Wire contract | `sw/include/astra/vfs_service.h` | — |
| Service core | `sw/userspace/vfs/src/vfs_service_core.c` | 1,740 B |
| Client Kit | `sw/userspace/vfs/src/vfs_client.c` | 1,414 B |
| lwext4 backend | `sw/userspace/vfs/src/vfs_ext4_backend.c` | 1,616 B |
| Same-process transport | `sw/userspace/vfs/src/vfs_local_transport.c` | 38 B |

`libastravfs.a` is built by `sw/userspace/Makefile` and is **not linked into
anything yet** — its consumer is the wiring on the WIP branch. Its tests
exercise it fully.

### 3.2 The rules the design turns on

- **Statuses are not errno.** An errno on the wire announces which backend is
  behind it and differs between implementations, which is the coupling the whole
  arrangement exists to prevent.
- **The request body is a union.** Every operation is addressed either by path
  or by an open handle, never both. That keeps two validation rules apart: a
  path must be NUL-terminated inside the record, and a payload must not be, or a
  full-width binary write is refused for lacking a byte it has no reason to
  contain.
- **A handle is a slot plus a generation**, and a file belongs to the session
  that opened it. Stale generations and cross-session attempts are counted
  separately on purpose: the first is a bug, the second is an attack.
- **Containment is checked mechanically, not asserted.** The core and the client
  have zero undefined `ext4_` symbols; the backend has twelve. `console_shell.c`
  on the WIP branch is compiled without lwext4 on its include path, so a
  filesystem call reappearing there fails to compile.
- **Records live in the client, not on the stack.** At 224 bytes each, a
  request/reply pair per frame is not affordable on a small stack. This took the
  Kit's frames from 488 bytes to 24.

### 3.3 What the protocol cannot do yet

- **Bulk transfer.** `ASTRA_MESSAGE_INLINE_MAX` is 256 bytes, so a read or write
  carries at most `ASTRA_VFS_IO_MAX` (192) inline. Section 5 of the architecture
  makes shared areas and bounded rings the LOCKED answer for bulk. Callers
  already loop over short transfers, so they do not change when it arrives.
- **A process boundary.** There is **no syscall that creates a process** —
  checked. The service therefore runs inside whoever mounted the volume. Every
  caller nonetheless goes through the protocol, so a loader plus a port
  transport moves it without touching a client.
- **Discovery.** The registrar is DIRECTION and unimplemented. Authority arrives
  through the startup capability table, which resolves a name to a handle with
  rights — the same shape as opening a service, minus asking at runtime.

### 3.4 One open call, not two

Recorded in `docs/DRIVER_AND_SERVICE_ARCHITECTURE.md` §9.1–9.3 and LOCKED there.
Exec needed `OpenLibrary` and `OpenDevice` because a library was a jump table in
the caller's address space; under protected address spaces that case does not
exist, so Astra has one open. The protocol version and the Kit version are
separate numbers, which is what Exec fused and what stopped an Amiga interface
moving independently of its implementation.

---

## 4. Stacks, as they are today

`KERNEL_THREAD_STACK_SIZE` is `0x3000` and `KERNEL_THREAD_STACK_STRIDE` is
`0x4000`, so each of 16 slots is three committed pages and one unmapped guard
page. That is 256 KiB of address space — page-table entries, not RAM — and 8 KiB
more committed per thread than before. The default boot runs one thread.

- `prepare_thread` maps the pages in a loop and unwinds exactly the ones it
  mapped, so a partial mapping leaves nothing behind.
- The reap path unmaps every page of the slot rather than the first.
- `user_stack_pages` counts pages now, not threads, and a static assert checks
  the count still fits the `uint8_t` holding it.
- Two assertions had the old assumption written out by hand and now derive from
  the constant: `thread.c` required a guard as large as the stack, which was the
  same thing as a guard page only while a stack was one page.

**A user-stack overflow presents as a fault just below `0x70000000`.** If a
panic reports a fault address a little under that, it is a stack overflow and
not a wild pointer.

---

## 5. What the review found, all committed

- **`--gc-sections` was never passed anywhere**, though every module compiled
  with `-ffunction-sections`. Turning it on returned **14,725 bytes of ROM** in a
  window fixed at 256 KiB in RTL, plus 8,192 bytes of kernel RAM, with BSS
  byte-identical. Most of what was shipping is legitimate test and introspection
  surface, so it is collected rather than deleted.
- **lwext4 patch 0004.** `ext4_fwrite` ended with
  `r = ext4_fs_put_inode_ref(&ref)`, discarding the write's own error. ENOSPC and
  device EIO both returned EOK **and took the commit branch**, journalling a
  transaction whose write had not happened. `ext4_fread` in the same file already
  did it correctly. `make ext4-test` gained a `full` mode that fills a volume;
  against unpatched upstream it fails with
  `ext4_fwrite returned EOK having moved 0 of 4096`.
- **The MBR reader accepted overlapping partitions.** The port confines a mount
  to the window the table hands out so a filesystem defect cannot reach the boot
  partition — but if the table itself overlaps, that window is legitimately
  inside the boot partition and every write is correctly permitted while
  `out_of_partition_refusals` stays zero.
- **The kernel is clean against its own LOCKED privilege boundary.** Every
  source and all 50 syscalls were checked against
  `DRIVER_AND_SERVICE_ARCHITECTURE.md` §2. `block.c` does validation, DMA
  ownership and slot accounting with **no scheduling**; input policy is already
  in `sw/userspace/input`; `CONSOLE_WRITE` is intentional, because §8 LOCKS that
  exclusive fullscreen "does not grant raw MMIO or remove validation". The one
  genuine exception is `elf.c` — §2 says Axiom does not parse application
  formats — which exists because the initial image loads before any filesystem
  or loader does. That exception is not written down anywhere as one.
- **`process.c` is not a grab bag.** 112 of its 113 functions are cohesive; one
  was 1,563 lines. The port family is lifted out; the dispatcher is 1,341.
- **The metrics registry has no shipped consumer.** `astra_metric_register` is
  called by nothing, the supervisor does not link the module, and `--gc-sections`
  collects both samplers. Every performance number in this repo comes from
  outside the machine. Fixing it needs a reader, and the reader is `PROC:`.

---

## 6. Traps that cost time in this session

- **A stale `sw/boot/astra_boot.bin` rsynced from the Mac onto Beast.** It boots
  as far as `POST PASS` and stops, which reads exactly like a broken measurement
  harness. This trap is already recorded in the other handover and it still
  caught the author of the timing harness. **Exclude build products from any
  rsync**, and rebuild in `sw/boot` before believing a boot failure.
- **The LP64 allocator class table used on an LP32 target** starves lwext4 and
  looks precisely like a big-endian journal defect: every transaction after the
  second silently loses its buffers. `sizeof(void *) == 4u` selects
  `astra_ext4_alloc_classes`; any new probe must do what `test_ext4_mount.c` does.
- **`-fanalyzer` is GCC-only** and the Mac's `gcc` is clang, so `make analyze`
  fails locally and must run on Beast.
- **`e2fsck` 1.47.0 does not accept `-E offset=`.** Use `image?offset=N` or a
  loop device. After a hard stop the volume needs one `e2fsck -fy` to replay the
  journal and fix the superblock counters; the second pass is what should be
  clean.
- **The Mac still cannot build the kernel** (Mach-O section attributes), as
  documented. It builds and tests everything in `sw/userspace` including the new
  VFS module.

---

## 7. Reproducing the gates

```sh
# userspace, including the VFS service core
cd sw/userspace && make test && make sanitize && make analyze && make all

# the filesystem, including the full-volume ENOSPC case added this session
cd sw/userspace/storage && make ext4-test

# kernel: 30 suites, both configurations
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1

# the ELF profile against the real gc-linked executable
ASTRA_ELF_FIXTURE=sw/userspace/supervisor/build/m68k/astra_supervisor.elf \
    sw/kernel/build/test_elf

# what the boot path costs, with a budget that fails if it regresses
python3 emu/qemu/time-boot.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --runs 5 --budget 1.0
```

Boot baseline on Beast, 5 runs, median seconds since launch: POST 0.02, kernel
handoff 0.02, user image 0.05, block round-trip 0.05, partition read 0.05, mount
and journal 0.08, volume verify 0.09, terminal up 0.09. Spread 0.08 to 0.10.
Host time for a fixed workload — **not 68030 time**, on Beast or the board.

The filesystem workload is deterministic and worth diffing directly:
`make ext4-test` reports 2,112 reads / 7,374 writes / 4,264 splits every run.
