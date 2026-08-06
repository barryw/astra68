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

**Everything is on `main`, and there are no other working branches.** The
branch layout this file described — `review/kernel-storage-hardening` and
`wip/vfs-terminal-wiring` — was merged and deleted on 2026-08-06. Work on
`main` directly. `main` is 15 commits ahead of `origin/main`; nothing is
pushed.

| Commit | What |
|---|---|
| `d751509` | `--gc-sections` everywhere; two dead kernel exports removed |
| `2eea811` | raised kernel warning gate, and the four sites it found |
| `3dd1ded` | lwext4 patch 0004: `ext4_fwrite` reporting failed writes as success |
| `3e8f792` | MBR reader refuses overlapping partitions |
| `34d6638` | port syscalls lifted out of the dispatch switch |
| `fa9c4f9` | boot-path timing harness and baseline |
| `79becbd` | the storage protocol and the service behind it |
| `27d8016` | three-page thread stacks (superseded by `bd35ad5`) |
| `30ce278` | the syscall records carry the alignment the kernel demands |
| `270bccf` | `emu/qemu/test-terminal.py`, the terminal's first gate |
| `d8e6f4c` | the terminal moved behind the storage protocol |
| `bd35ad5` | reserve-and-grow thread stacks |
| `e20eb85` | `make coverage` over the kernel's host suites |

Every gate is green on Beast: 30 kernel suites in both configurations,
userspace test/sanitize/analyze/cross-build, `ext4-test`, the ELF fixture at 84
pages, the boot budget at 0.08s of 1.00s, and the terminal gate.

Kernel line coverage over the host suites is **82.8% of 14,197 executable
lines** (`make coverage` in `sw/kernel`). Weakest: `monitor.c` 65.4%,
`process.c` 77.6%.

---

## 2. Do these next, in this order

### 2.1 The terminal hang — found and fixed, 2026-08-06

Fixed by `30ce278` on `review/kernel-storage-hardening`. The wiring on
`wip/vfs-terminal-wiring` was never the problem, and the lead recorded here
before it was diagnosed was wrong in both halves. Both are written out below,
because each cost time.

**The cause was the ABI, not the wiring.** Six syscalls refuse a user buffer
whose address is not four-byte aligned. Nothing gave the records that
alignment, and the m68k ABI aligns `uint32_t` to **two** bytes — so every one
of them was four-byte aligned only by luck of where the linker or a stack
frame put it. Moving the input batch out of `console_shell_run`'s frame put it
in `.bss` at `0x0014c5d2`. From then on every `ASTRA_SYSCALL_INPUT_READ_TRY`
returned `INVALID_ARGUMENT` before the kernel ever reached the queue, and the
shell — which treated any non-OK read as "no input" — yielded and looped
forever.

The fix puts `_Alignas` on the first field of each record, so the requirement
lives with the type rather than with wherever an instance lands, and asserts it
statically. The assert is a **multiple**, not an equality: the two block
records hold a `uint64_t` and are eight-aligned on LP64, which is what caught
the first version of the fix on the host build. `console_shell_run` now treats
only `WOULD_BLOCK` as "no input" and reports any other status like a failed
flush.

**Two things that made it read as a hang, and will do so again:**

- **A refused syscall and an empty queue looked identical to the caller.**
  Nothing was logged, nothing faulted, and the boot log reaches `stage 8`
  whether the terminal works or not, so every existing gate passed.
- **The lead in this file was an artifact.** "The prompt renders exactly two
  characters and stops" is **the pre-existing state of the boot banner on
  main-line too** — see section 6 — so it pointed at the flush path when the
  flush path was fine.

The gate that closes the hole is `emu/qemu/test-terminal.py`: it types into the
machine over QMP, reads the character plane back, and asserts both that a file
round-trips and that the Vesta input FIFO drains. It fails against the unfixed
build and passes against the fixed one, on both branches.

What had already been ruled out, and stayed ruled out:

- **Not the fault this replaced.** At one page the chain overflowed at
  `0x6FFFFFEC`, twenty bytes below the `0x70000000` stack base. That is fixed.
- **Not the Kit's stack cost.** The client's in-flight records live in the
  client, not on the stack; its frames are 24 bytes.
- **Not `console_shell_run`'s frame.** Moving the input batch out of it took it
  from 1,624 bytes to 84. GCC then inlines the commands into `run_line`, whose
  frame is 1,260, and that is where the remaining stack goes.

**`wip/vfs-terminal-wiring` works** with the fix under it: `mkdir`, `write`,
`ls` and `cat` all round-trip through the storage protocol to the card, judged
by `e2fsck` and `debugfs` on the image afterwards. It has not been rebased or
merged; its commit message still says it hangs, and that message is now wrong.

### 2.2 Reserve-and-grow stacks — done, 2026-08-06

Landed as `bd35ad5`. A slot is now a 64 KiB **reservation** with an unmapped
guard page at its floor; a thread starts with one committed page at the top and
the fault handler commits more as the stack reaches them. Address space costs
page-table entries rather than RAM, so no thread needs a size chosen for it,
and a wild pointer still dies on the guard.

The blocker recorded here was real but understated. Three things had to change,
not one:

- **`kernel_process_on_fault` answers the fault** rather than retiring the
  process. Resuming works because the captured context already carries the
  faulting instruction's own program counter, so re-entering user mode re-runs
  the access. That is a property of the machine, not a law: QEMU restores the
  PC to the faulting instruction before stacking the format-B frame, and its
  RTE pops that frame and resumes there rather than rerunning a bus cycle.
  A machine that reran the cycle would need the frame handed back intact.
- **`dispatch_user_fault_fast` scheduled the process worker unconditionally**,
  because every user fault used to be a death. The first growth that worked
  panicked here with `process worker missing teardown`. It now schedules on the
  same condition the syscall path uses.
- **`kernel_copy_to_user` grows and retries once.** When a syscall writes into
  a fresh frame the kernel reaches the page before the user does, so the user's
  fault handler never sees it and a good stack address would have come back
  `BAD_ADDRESS`.

Measured on the machine, not argued: a supervisor made to touch 12 KiB of stack
it was never given commits **three pages in one fault, with zero user faults**,
and carries on into a working terminal; one made to write into the guard page
dies as before. The counters are `user_stack_growths` and
`user_stack_pages_committed` in `KernelSchedulerStats`, kept apart from
`user_faults` because one is the system working and the other is a process
dying.

**The shell itself no longer grows at all.** Moving its input batch out of the
frame took the deepest chain under a single page, so the three-page commit that
prompted this work is not needed by the thing that prompted it. That is the
point: nothing had to be sized for it. It also means the terminal gate does not
exercise growth — the probes above are what do, and they are temporary by
design. If you need to re-run one, insert it in `main.c` before
`console_shell_run` and read the counters through the QEMU monitor.

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

`KERNEL_THREAD_STACK_STRIDE` is `0x10000` and `KERNEL_THREAD_STACK_SIZE`, now
the **initial commit**, is `0x1000`. Each of 15 slots is one guard page at the
floor, up to 15 growable pages above it, and a stack pointer starting at the
top of the stride. That is 960 KiB of address space — page-table entries, not
RAM — against one committed page per thread. The default boot runs one thread
and never grows.

Layout, per slot:

```
slot_base                                          slot_base + STRIDE
|                                                                   |
[ GUARD ][ . . . . grows down into here . . . . ][ committed at start ]
[ 1 page]                                        [       1 page       ]
```

- `prepare_thread` maps the initial pages at the **top** of the slot and unwinds
  exactly the ones it mapped, so a partial mapping leaves nothing behind.
- `grow_user_stack` commits the whole span between the faulting address and
  what the thread holds, because a frame larger than a page can touch its
  bottom first and a hole would make `user_stack_base` describe pages that are
  not mapped.
- The reap path unmaps `thread->stack_pages`, what this thread grew to, not a
  constant. The count fits padding the record already had, so it costs no
  kernel RAM.
- `user_stack_pages` is bounded by the fully grown worst case now, not the
  committed one, and a static assert checks it still fits its `uint8_t`.

**A user-stack overflow presents as a fault in the guard page at the bottom of
the slot** — for the first thread, just under `0x70001000`. A fault below a
slot's floor is a real overflow or a wild pointer; one above it is answered by
growth and never reaches a panic.

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

- **A user buffer handed to a syscall must be four-byte aligned, and the m68k
  ABI will not give it to you.** `_Alignof(uint32_t)` is 2 on this target, so a
  record built from `uint32_t` fields aligns to 2 unless it says otherwise.
  Every ABI record the kernel copies now carries `_Alignas(ASTRA_ABI_ALIGNMENT)`
  and asserts it. **Anything new that crosses the syscall boundary must do the
  same**, or it will work from a stack frame and fail from `.bss`.
- **The boot banner is drawn wrong and it is not a rendering bug worth
  chasing from the screen.** The first paint comes out as `co` on one row, the
  second help line one row above where it belongs, and the prompt split across
  two rows — on `main`, on `review/kernel-storage-hardening` and on the WIP
  branch alike. **It is cosmetic and it self-heals**: `clear` followed by any
  command renders perfectly, and every command after the first renders
  perfectly. The terminal's own logic is not at fault; the character plane is
  shared with the kernel's boot console, which is still writing progress lines
  to it while the shell paints. Not diagnosed further. Do not use it as a lead
  — the last session did, and it pointed away from the real defect.

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

# the terminal, end to end: types over QMP, reads the character plane back
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img

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
