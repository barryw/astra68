# Astra 68 — Handover: the loader, and what is left of milestone 1

Date: 2026-08-07, updated 2026-08-06 after tasks 2 and 2b. Written to be read
cold in a fresh session.

A program can launch a program, a launcher has wrappers for it, and every image
now declares what it is. The three tasks left turn that into `COMMANDS:events`
running from the prompt.

**Everything is on `main`.** `origin/main` is still at `d1fef0c`; everything
since is local and unpushed.

---

## 1. Resume here

**Task 4 of `docs/superpowers/plans/2026-08-07-launch-milestone-1.md`.**

The plan has six tasks and one was added while task 1 was being built:

| Task | What | State |
|---|---|---|
| 1 | the launch syscall | **done**, `3090f1a` |
| 2 | runtime wrappers, and seeding an assign table from a capability table | **done**, `d0bd9a3` |
| 2b | `ASTRA_PROGRAM`, mandatory provenance, link fails without it | **done**, `5be6f15` |
| 3 | streams — `STDOUT`, `STDERR`, `STDIN` as grants, not numbers | **done**, `c9058d9` |
| 4 | the shell launches by name; `COMMANDS:` bound; `status` proves it | next |
| 5 | the storage protocol over ports | |
| 6 | `events` becomes `COMMANDS:events`; the builtin is deleted | |

Each finished task has a "what the build settled" block under it in the plan.
Read task 2's and 2b's before task 3 — one of them is a decision task 3 has to
make, below.

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

## 4. What tasks 2 and 2b built, and the one decision they left task 3

**Task 2** — `astra_launch` and `astra_process_wait` in
`sw/userspace/runtime/src/launch.c`, and `astra_assign_seed` in
`sw/userspace/vfs/src/vfs_assign.c`.

- `astra_process_wait` takes a **deadline** the plan's signature did not, and
  zero polls. The supervisor hosts the services its child calls, so it may never
  block on one; task 4's serving wait is a poll in the loop that already pumps
  everything else.
- Both wrappers clear their outputs before anything, including before their own
  refusal. `exit_status` is published only when the wait established one.
- `astra_assign_seed` binds every published capability except `PROCESS` and
  `THREAD`. Roots do not travel in the published table yet — the launch spec's
  §2 has `root_offset` in the grant and `AstraStartupCapability` has nowhere to
  put one — so every binding is at its mount's own root.

**Task 3** — `sw/include/astra/stream_service.h`, `sw/userspace/streams/`,
`sw/userspace/supervisor/src/console_stream.c`, and the runtime's port wrappers
in `sw/userspace/runtime/src/port.c`.

- The grant-flag question is **settled**: `ASTRA_CAPABILITY_FLAG_NAMESPACE` is
  carried by the kernel from the grant into the published record, and
  `astra_assign_seed` binds only the grants that declared themselves names. A
  launch that wants a child to have `WORK:` must set that bit; a `STDOUT` grant
  must not. Unknown flag bits are `INVALID_ARGUMENT` at the syscall.
- `astra_stream_write(handle, bytes, length, &written)`, `astra_print(handle,
  text)` and `astra_stream_read(source, bytes, capacity, &length)` are the
  client. **`astra_print` takes a handle** — there is no ambient output.
- `console_stream_stdout()` / `_stderr()` / `_stdin()` are the send handles a
  launch grants. They exist before the first prompt, because a child is handed
  what its launcher already holds.
- **`console_stream_offer` is called by nobody.** That is `STDIN`'s only missing
  piece and it is task 4's: while a child runs, the line the editor finishes
  goes to the source instead of to `run_line`.

**The trap task 3 found, which task 5 will meet again.** Attaching a handle to a
port message **moves** it — ports carry no retain, so the sender's entry is
invalidated. A reply handle cannot be cached across calls; `astra_stream_read`
makes a reply port per read. The streams mock models this deliberately, and a
test asserts the mock does, because a mock that copied is what let the broken
version pass.

**Task 2b** — `sw/include/astra/program.h`, the `ASSERT` at the end of
`sw/userspace/runtime/astra_user.ld`, and `tools/program_info.py`.

- `ASTRA_PROGRAM(name, major, minor, patch, author, copyright)` emits one
  120-byte record into `.astra_program`. No record, no link.
- The record is **loaded** and survives the strip, unlike the event catalog:
  `python3 tools/program_info.py <any Astra ELF>` works on the installed image.
- `make link-contract` in the runtime is the check, and it runs inside
  `make all`. Every new program in `sw/userspace/commands/` needs one
  `ASTRA_PROGRAM` line or it will not link — that is the point, but it is also
  the first error task 4 will hit.

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

On the Mac: `python3 -m pytest tools/tests/` (38 cases) and
`python3 -m pytest sw/boot/tests/`. **Reap QEMU** after a gate —
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
