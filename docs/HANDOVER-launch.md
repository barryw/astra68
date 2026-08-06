# Astra 68 — Handover: the loader, and what is left of milestone 1

Date: 2026-08-07. Written to be read cold in a fresh session.

A program can launch a program. `ASTRA_SYSCALL_PROCESS_CREATE` landed with the
rule that makes it safe — a launch creates no authority — and the five tasks
after it turn that into `COMMANDS:events` running from the prompt.

**Everything is on `main`.** `origin/main` is still at `d1fef0c`; everything
since is local and unpushed.

---

## 1. Resume here

**Task 2 of `docs/superpowers/plans/2026-08-07-launch-milestone-1.md`.**

The plan has six tasks and one was added while task 1 was being built:

| Task | What | State |
|---|---|---|
| 1 | the launch syscall | **done**, `3090f1a` |
| 2 | runtime wrappers, and seeding an assign table from a capability table | next |
| 2b | `ASTRA_PROGRAM`, mandatory provenance, link fails without it | added 2026-08-07 |
| 3 | streams — `STDOUT`, `STDERR`, `STDIN` as grants, not numbers | |
| 4 | the shell launches by name; `COMMANDS:` bound; `status` proves it | |
| 5 | the storage protocol over ports | |
| 6 | `events` becomes `COMMANDS:events`; the builtin is deleted | |

Design authority: `docs/superpowers/specs/2026-08-07-program-launch-design.md`,
and `2026-08-06-filesystem-layout-design.md` §2.5 (lookup), §11 (what a file is)
and §1.7 (union assigns, approved 2026-08-07 — that is milestone 1.5, after this
one).

## 2. What task 1 built, that task 2 builds on

```
ASTRA_SYSCALL_PROCESS_CREATE (48)
  data[1] image address, in the caller's memory   data[2] length
  data[3] AstraLaunchGrant array                  data[4] count
  data[5] AstraLaunchArguments block, or 0
returns
  data[1] a process handle: QUERY | WAIT | TERMINATE, never DEBUG
  data[2] the new process id
```

- `AstraLaunchGrant` and `AstraLaunchArguments` are in `sw/include/astra/process.h`;
  `ASTRA_LAUNCH_GRANT_MAX` is 6, `ASTRA_LAUNCH_ARGUMENT_MAX` 8, and the argument
  bytes are bounded at 192.
- **The subset rule was already implemented.** `kernel_handle_duplicate`'s two
  checks — the source must carry `TRANSFER`, the rights must be a subset — are
  exactly §1.1 of the launch spec. It grew a destination table
  (`kernel_handle_duplicate_into`) and the launch grew nothing.
- A handle the caller does not hold is `INVALID_HANDLE`; rights wider than its
  own are `ACCESS_DENIED`. Two different mistakes, told apart on purpose.
- **argv is already published** into the child's startup page, after the
  capability table: a vector of addresses, then the bytes. `astra_main` gets it
  through `AstraStartupInfo.argc` / `argv_address`, which the runtime already
  validates.
- `kernel_elf_accept_windowed` bounds the headers to the bytes actually handed
  over, and each segment page bounces through one page of kernel memory
  (`launch_page`). A launched image is never read through a user pointer.

## 3. The finding that changes task 5

**Only a cloneable object can be granted.** A copy needs a retain, and areas,
IRQ endpoints and devices have one — installed with
`kernel_handle_install_cloneable`. **Ports do not.** They are installed with
`kernel_handle_install` and move through the transfer machinery instead.

So before a child can be handed a service handle, `kernel_port_handle_retain`
has to exist and the port endpoints have to be installed cloneable. That is
task 5's first step, not a surprise in the middle of it.

## 4. Task 2, concretely

**Files:** `sw/userspace/runtime/include/astra/runtime.h`,
`sw/userspace/runtime/src/launch.c` (new),
`sw/userspace/runtime/tests/test_runtime.c`,
`sw/userspace/vfs/src/vfs_assign.c`

1. `astra_launch(image, length, grants, count, arguments, &handle, &id)` and
   `astra_process_wait(handle, &status)` — thin wrappers, tested against the
   syscall mock in `test_runtime.c`, which asserts what each register carries.
2. Seeding: a launched program's namespace *is* its capability table, so the
   runtime turns `AstraStartupCapability[]` into an `AstraAssignTable`. Name and
   rights come straight across; **roots do not exist in the published table yet**
   — the launch spec's §2 has `root_offset` in the grant and the published
   `AstraStartupCapability` has nowhere to put one. Bind at the mount root for
   now and add the root when the first grant needs one, which is a deliberate
   deferral rather than an oversight.

## 5. Working on this machine

Ship and build, always from the repo root, and **always rebuild the boot image
after userspace** — the ROM carries the user image, so a rebuilt supervisor that
is not re-ROMmed boots the previous one and every event id resolves to the wrong
message:

```sh
git add -A                      # tar ships only what git tracks
git ls-files -z | tar --null -T - -czf /tmp/astra-src.tgz
scp -q /tmp/astra-src.tgz beast:/tmp/
ssh beast 'cd ~/astra68 && tar xzf /tmp/astra-src.tgz && \
  find sw -name "*.[ch]" -exec touch {} +'
```

The gates, all on Beast:

```sh
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1
cd sw/userspace && make test && make sanitize && make analyze && make all
cd sw/userspace/storage && make ext4-test
cd sw/boot && make astra_boot.bin
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img
python3 emu/qemu/test-events.py  ... same arguments
python3 emu/qemu/time-boot.py    ... --runs 5 --budget 1.0
```

On the Mac: `python3 -m pytest tools/tests/`. **Reap QEMU** after a gate —
`pkill -f qemu-system-m68k` — because a lingering emulator makes the next gate
look like a machine that will not boot.

`docs/HANDOVER-events.md` §6 has the rest of the traps, and they all still
apply: `git ls-files | tar` ships only tracked files and only from the directory
you are standing in, m68k aligns a `uint32_t` to two bytes, a journal replay
lands on top of anything `debugfs` wrote.

## 6. Where the machine is otherwise

The event system is finished — six plans, `EVENTS:` is a synthetic tree and
`events` reads it. The console sink stops narrating once something drains the
ring, so a terminal capture is readable again. The one promise the event system
does not keep is durability: the store is RAM, so `EVENTS:boot/-1` does not
exist. That is the next thing after this milestone and milestone 1.5.
