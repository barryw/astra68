# Astra 68 — Handover: a C library, real commands, and the limits that held them back

Date: 2026-08-19. Written to be read cold. Read `CLAUDE.md` first, then
`docs/HANDOVER-launch-latency.md`, which the same session finished and which
covers the compositor half of this work.

**Where it stands.** Astra has a C library, a POSIX layer, `ls` and `ps` as real
programs, process state as a filesystem, and a kernel sized for a dozen services
and a dozen programs at once. **One kernel test fails** and it is the first thing
to fix — §7. Nothing is committed.

---

## 1. What this is for

The goal is Unix software on Astra, and the first target named was **vim**. That
sets the shape of everything below: not GNU coreutils (GPL, gnulib, 443 KiB per
binary before it does anything), but a small permissive libc plus a POSIX layer
written against Astra's own capabilities, with the commands as native programs.

The measurement that decided it:

```
static glibc hello world, m68k, stripped   443,152 bytes
COMMANDS:hello on picolibc                  12,238 bytes
existing hand-rolled commands           9,104 – 30,600 bytes
```

## 2. picolibc

Vendored at `third_party/picolibc`, tag **1.8.12**, commit
`2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`. 19 MB: upstream's 47 MB `test/`
tree is removed because the build only descends into it with `-Dtests=true` and
those tests execute m68k binaries, which no host here can do.

**No upstream source file is patched.** The only Astra file is
`scripts/cross-m68k-astra.txt`, and it exists because upstream's m68k cross file
targets `-march=68020` with the toolchain's default float ABI while Astra builds
`-m68030 -msoft-float` — a libc built for a different float ABI links without a
complaint and returns wrong answers.

Build it with `mk/build-picolibc.sh` (installs to `~/picolibc-astra`; point
`PICOLIBC` elsewhere if you want). It needs `meson`, which is **not** in Ubuntu's
repos at a new enough version on `beast` — it was installed with
`python3 -m pip install --user --break-system-packages meson`, so
`~/.local/bin` must be on `PATH`.

Licensing was a selection criterion, not a discovery: picolibc is
BSD-3-Clause/MIT throughout with all non-BSD-compatible source removed upstream.
coreutils is GPLv3 and BusyBox GPLv2, which is why neither is here.

## 3. The POSIX layer

`sw/userspace/posix`, its own library. **The rule it keeps: a descriptor is an
index into a table this process owns, and every entry holds a capability the
process was already granted.** Nothing invents authority — a program not handed
`STDOUT` has no fd 1, and that is the correct answer rather than a missing
feature.

`src/console.c` is all of it today: `write`, `read`, `close`, `isatty`, `lseek`,
`_exit` over the `STDIN`/`STDOUT`/`STDERR` stream capabilities. `read` waits by
yielding rather than returning zero, because a stream's "nothing ready" is not
POSIX's end of file and a reader that treats it as one stops at the first idle
moment.

`sw/userspace/posix/README.md` carries the full table of what is missing, what
Astra's answer for each is, and which program needs it first. The short version
for vim, in the order it will ask:

1. `tcgetattr`/`tcsetattr` and `ioctl(TIOCGWINSZ)` — raw mode is the first thing
   it does
2. `sigaction`, `select`/`poll` — `SIGWINCH` and input
3. `open`/`stat`/`opendir`/`readdir` over the VFS
4. `fork`/`execve`/`waitpid`/`pipe`/`dup2` for `:!`

The exact set picolibc wants from us, minus what libgcc already provides, came
from the linker and is **17 functions**. It is listed in the README.

## 4. Storage protocol version 6

`AstraVfsReply` carries `mode`, `uid`, `gid`, `mtime` and `nlink`. Before this a
reply carried a size and a kind, so `ls -l` was not something the shell declined
to do — it was something nothing on the machine could do.

**The metadata rides on the READDIR_BATCH entry, not on a stat.** A cross-process
round trip is about 7.5 ms here, so a forty-name `ls -l` that stat'd each entry
would spend a third of a second switching address spaces. The entry format is
documented at `ASTRA_VFS_DIRENT_HEADER` in `sw/include/astra/vfs_service.h`:
32 bytes, big-endian field by field, then the name.

The backend gets it from **one** inode lookup. `ext4_meta_get()` was added to
lwext4 (`third_party/lwext4`, recorded in its `ASTRA_VENDOR.md`) because
`ext4_mode_get`, `ext4_owner_get` and `ext4_mtime_get` each open the path
separately — four lookups per file, and `ls -l` would pay four per entry. It
also returns the link count and size, which no public getter exposed.

**Zero means the backend does not carry the field, not that the field is zero.**
`ls` prints `?????????` for mode 0 rather than `---------`: a filesystem with no
permission bits and a file with none are different facts.

**The reply record is now exactly 280 bytes, which was the message ceiling at
the time.** The ceiling has since been raised (§6), so that is no longer tight —
but the batch entry is still sized against the old one, so a message carries
about four entries. When a large listing measures badly the fix is READDIR into
the bound transfer area the way `READ_PATH` already does, not a smaller record.

## 5. The commands, and PROC:

| program | size | what it proves |
|---|---:|---|
| `COMMANDS:hello` | 12,238 | picolibc's stdio reaches a stream capability. Self-checking: formats with `snprintf`, verifies width, sign and zero-padded hex byte by byte, returns 10/11/12 on failure. The verdict is the exit status because text lands on a screen and status lands in the trace ring. |
| `COMMANDS:ls` | 36,714 | protocol v6 end to end. `-l` shows mode, links, uid, gid, size, date. |
| `COMMANDS:ps` | 36,798 | `PROC:` end to end. |

`ls` replaced the shell builtin, which was removed from `console_shell.c`. Note
that **`console_shell.c` lives in the supervisor but is compiled into the
terminal service** — it is the terminal's shell, not dead code, though the
supervisor's own boot path no longer reaches it (`main.c` goes straight to
`supervisor_loader_watch`).

**`PROC:` is real.** `sw/userspace/supervisor/src/proc_tree.c` is a VFS backend
over the supervisor's own process handles, served on its own port and bound as an
assign that children inherit like any other mount. Layout is
`PROC:<id>/status`, per `docs/OBSERVABILITY.md`; `mem`, `cpu` and `threads` are
specified there and not built yet.

It is a service and not a syscall on purpose. `PROCESS_INFO` is scoped to a
handle the caller already holds so that nobody enumerates by counting upwards;
the supervisor can answer because it holds the handles, and a program sees the
tree because it was granted the mount. `ps` prints a **GEN** column for the same
reason — a bare number must never name a process, which is the race `kill 42`
loses on other systems.

## 6. Kernel limits

The kernel does no dynamic allocation, and that stays: a process that could make
the kernel allocate could exhaust the kernel, and a kernel out of memory is not a
failed allocation, it is the machine. Every resource keeps its **pair** — a
global budget and a per-owner quota — so one process cannot spend another's
share. What changed is that the numbers are now sized for the workload.

| | before | after |
|---|---:|---:|
| processes | 15 | **32** |
| threads | 16 | **32** |
| ports, global / per owner | 24 / 6 | **128 / 24** |
| messages, global / per owner | 72 / 40 | **256 / 64** |
| message inline payload | 256 B | **1024 B** |
| port queue | 8 msgs, 2,240 B | **16 msgs, 16,768 B** |
| handles per process | 48 | **128** |
| rings, global / owner | 16 / 4 | **64 / 16** |
| sync objects, global / owner | 32 / 8 | **128 / 32** |
| devices / IRQ endpoints | 8 / 16 | **16 / 32** |

**Kernel image 129,896 bytes, essentially unchanged.** All the growth is BSS in
reserved regions, so none of it costs ROM.

### Three of those were field widths, not budgets

This is the part worth knowing before touching them again.

- **15 processes** because a shared frame's alias count had **four bits** of a
  one-byte-per-frame ledger (`mapped_user_frames` in `vm.c`). It is `uint16_t`
  now, split eight and eight: 32 KiB more across 128 MiB.
  `KERNEL_VM_SHARED_ALIAS_MAX` now follows `KERNEL_VM_ADDRESS_SPACE_MAX`,
  because an alias is an address space that has the frame mapped and there
  cannot be more of them than there are address spaces.
- **16 threads** for two independent reasons. The reap and wake bitmaps were
  `uint16_t`, and the thread identifier packed the slot in **four bits**
  (`generation << 4 | slot`), so a seventeenth thread would have produced a
  repeating id. Bitmaps are 64-bit; the id gives the slot six bits and keeps
  eighteen of generation.
- A **variable 64-bit shift calls `__ashldi3`, and the kernel links no libgcc.**
  Both bitmaps use a `slot_bit()` helper that splits at the word boundary so the
  shift stays constant and inline. Do not write `1ull << slot` in kernel code.

Two more traps from the same pass:

- **Every process embeds a handle table**, so `KERNEL_HANDLE_MAX_ENTRIES`
  multiplies by `KERNEL_PROCESS_MAX` and is the largest term in a process
  record. Raising it to 192 quadrupled the record. It is sized against
  `KERNEL_PROCESS_HANDLE_DEMAND`, which computes to 99 and fails the build by
  name if a new grant outgrows it.
- **GCC emits `memset` for `= {0}` on a four-word array** and for a loop that
  fills every word of one. The kernel links no C library. Use
  `kernel_bytes_clear`, and write fills as "whole words, then the tail".

### Where the space came from

The kernel had **12 KiB free of its 512 KiB**. Rather than grow that region —
which would have moved the trace ring, and crash tooling reads it at a fixed
address — a **`TABLES` region** was added *above* the frame metadata:

```
KERNEL 0x02044000  512K   unchanged
TRACE  0x020c4000   64K   unchanged, tooling safe
META   0x020d4000  512K   unchanged
TABLES 0x02154000    2M   new
usable RAM now starts 0x02354000   (was 0x02154000)
```

Only that last boundary moved: `ASTRA_KERNEL_TABLES_ADDRESS`,
`ASTRA_KERNEL_RESERVED_SIZE` and `ASTRA_KERNEL_USABLE_ADDRESS`/`_SIZE` in
`sw/include/astra/boot.h`, and the `MEMORY` block in `sw/kernel/kernel.ld`.

Thread stacks, ports, messages, processes, rings, sync objects, detached handle
entries, areas, area mappings and `mapped_user_frames` all moved there with a
`KERNEL_TABLES` section attribute. Current use: **kernel region 200 KiB of
512 KiB, TABLES 1.34 MiB of 2 MiB.** `kernel.ld` fails by name if TABLES
overflows.

**`_kernel_bootstrap_end` must be taken before the thread-stacks section**, now
that those live in TABLES — the location counter follows them out of the region
and a bootstrap end measured after them is an address in a different region. That
cost a confusing "kernel exceeds its reserved bootstrap region".

## 7. The one thing that is red

`sw/kernel/tests/test_process.c`,
`test_real_handle_exhaustion_rolls_back_thread_create`. **29 of 30 kernel test
binaries pass; this one aborts.** `sw/userspace` is 23/23 green.

The test fills a process's handle table until allocation fails and checks that a
failed `THREAD_CREATE` rolls back cleanly. It computes how many children to
create from what is left of the table after every other owner limit takes its
share. With 128 entries **the handle table is no longer the scarcest resource**,
so it cannot reach "full" the way it is written.

Two bounds were already added and are correct to keep — rings are bounded by
areas (a ring is created over an area, and `KERNEL_RING_OWNER_MAX` now exceeds
`KERNEL_AREA_OWNER_MAX`), and children are bounded by process slots. What is
still needed is for the test to exhaust the table deliberately rather than as a
by-product of owner quotas: install plain objects until `KERNEL_HANDLE_OK` stops
coming, then attempt the thread create. That keeps the property it is really
testing — rollback on a full table — independent of which resource happens to be
scarcest.

**Do this first.** Nothing else should be built on a red kernel gate.

## 8. Machines, and what is where

Built on `beast` in `~/astra68-verify`, which is an rsync copy, not a checkout.
The Mac cannot build the kernel or link `sw/userspace/services` host tests
(no `--gc-sections`).

Board `astra-arty`, reached as root **from `beast`**:

| file | what |
|---|---|
| `/data/astra/storage-cmds.img` | the build with v6, `ls`, `ps`, `hello`, `PROC:` |
| `/data/astra/rom/astra_boot-cmds.bin` | its ROM |
| `/data/astra/bin/astra-terminal-display.profcmd` | the ARM display driver with per-command profiling; the stock binary was never overwritten |
| `/data/astra/run/drive2.py` | QMP driver: `launch` double-clicks Terminal, `type "..."` types a line |

Host QEMU for screen-free work:
`~/.cache/astra68/qemu-9.2.4/build-host-*/qemu-system-m68k`. It had to be built
with `ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4` because the default
work root is on the NAS and meson refuses on a sub-second clock skew.

**Verifying a command without a screen**: put it in the startup manifest as a
required `application` and read the supervisor's exit status off serial. A
required entry that exits before publishing ready has its status returned by
`launch_entry`, so the kernel prints it. `ps` returning 0 surfaces as
`status 0x00000010` (`PEER_DEAD` with a zero child status) — that is how `PROC:`
was proved end to end. `/tmp/run-ps.py` on `beast` does it.

## 9. Traps this session paid for

- **A storage image killed mid-run will not boot again until it is fsck'd.**
  Killing QEMU rather than shutting down leaves a dirty ext4 journal; lwext4
  replays it and hands back bad bytes. It surfaces a long way from the cause:
  `astra_launch:2: failed` on whichever service is read next, supervisor exits
  `ASTRA_STATUS_INVALID` (8), kernel panics with *initial user image exited*. The
  file on the volume is byte-identical; only metadata is wrong. `e2fsck -fy` on
  the sliced-out volume fixes it. Also recorded in the launch-latency handover.
- **`emu/qemu/test-terminal.py` fails on host QEMU, and did before this work** —
  a clean `~/astra68-final` tree fails identically, `SERVICES:terminal` exits and
  the supervisor returns `0x10`. That gate is the natural dev loop for libc work
  and it is broken. Worth fixing before the vim port.
- `astra_image.install()` defaults to the **terminal** manifest. A desktop image
  needs `service_names=astra_image.DISPLAY_SERVICES` and
  `manifest_text=astra_image.DISPLAY_STARTUP_MANIFEST`, or the machine boots
  something else and dies with a status that looks like a code fault.

## 10. Ranked next steps

1. **Fix the handle-exhaustion test.** §7. The kernel gate is red until it is.
2. **Fix `test-terminal.py` on host QEMU.** §9. Without it every command needs a
   board and an eyeball, which will not scale across a vim port.
3. **The POSIX file and directory half** — `open`, `stat`, `opendir`/`readdir`,
   `getcwd`/`chdir`. `ls` and `ps` reach the filesystem through the Astra library
   directly; vim will not.
4. **Terminal, signals and `select`** — the vim-critical set, §3.
5. **Charge kernel objects to the owner ledger.** The agreed destination is that
   the kernel reserves what it needs to stay alive and applications get the rest:
   quotas proportional to free memory rather than constants, with the kernel's
   own floor untouchable. `sw/kernel/memory.c` already has the ledger and an
   emergency reserve, so the missing half is charging kernel-side allocations to
   it. The per-owner caps kept in §6 are what survives into that design — they
   are what makes dynamic safe rather than dangerous.
6. **READDIR into the transfer area.** §4, once a large listing measures badly.
