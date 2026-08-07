# Astra 68 — Handover: milestone 1, and the one thing left in it

Date: 2026-08-07. Written to be read cold in a fresh session. Read `CLAUDE.md`
first; this is the continuation map for the launch milestone only.

**A program runs from the prompt.** `status 7` is a file on the volume, found
by name, loaded, run, and its exit status reported. A launched child is handed
six capabilities: `STDOUT`, `STDERR`, `STDIN`, `WORK:`, `COMMANDS:` and
`EVENTS:` — the last three as port handles to services it reaches across a
process boundary.

**One task is left and it is blocked on a bug.** `events` is now a program too,
and its first request to the events service is never answered. §2 is the whole
of what is known about that, and it is where to start.

---

## 1. Where the code is

| Where | What | State |
|---|---|---|
| `main` | tasks 1–5 | **pushed**, `ec75bb4`. Every gate green. |
| `task6-events-program` | task 6 | `7d211f7`. **The terminal gate fails.** |

`main` was pushed to `origin` at `435d530..ec75bb4`, 48 commits. Task 6 is on a
branch on purpose: it does not pass, and `main` stays green.

The plan is `docs/superpowers/plans/2026-08-07-launch-milestone-1.md`. Every
finished task has a **"what the build settled"** block written under it — those
blocks are the real record and are worth reading before touching what they
describe.

| Task | What | State |
|---|---|---|
| 1 | the launch syscall | done, `3090f1a` |
| 2 | runtime wrappers, seeding a namespace from a capability table | done, `d0bd9a3` |
| 2b | `ASTRA_PROGRAM`, mandatory provenance, link fails without it | done, `5be6f15` |
| 3 | streams — `STDOUT`/`STDERR`/`STDIN` as grants, not numbers | done, `c9058d9` |
| 4 | the shell launches by name; `COMMANDS:` bound; `status` proves it | done, `fc8a643` |
| 5 | the storage protocol over ports | done, `3d66e07` |
| 6 | `events` becomes `COMMANDS:events`; the builtin goes | **blocked**, §2 |

---

## 2. Resume here: the refusal nobody has explained

On the branch, type `events` at the prompt. The child launches, is granted
`EVENTS:`, and hangs until it gives up. The chain is understood up to exactly
one step.

```
child:      astra_port_send(EVENTS: handle, request, 248 bytes, +1 handle) -> OK
supervisor: astra_port_receive(events_receive, ...)  -> RESOURCE_LIMIT, every pass
child:      the second send fills the one-deep port  -> WOULD_BLOCK
child:      exits 14 (ASTRA_VFS_ERR_BUSY)
```

The message **is** queued on the right port. The supervisor **cannot take it
out**. It retries every loop pass and gets `ASTRA_SYSCALL_RESOURCE_LIMIT` (5)
forever.

**How to see it.** The branch prints the evidence on the terminal already:

- `events: probe handle N / probe create 0 / probe send 0` — a raw
  `astra_port_send` from the child, which succeeds. In
  `sw/userspace/commands/events/events.c`.
- `  [events served 0, refused 0, stalled 5` — printed by `command_launch`
  after any child exits. `stalled` is the last non-`WOULD_BLOCK` status the
  service's receive returned.

**What has been ruled out.** The per-process handle table. It was 16 entries
and the supervisor now holds eight port endpoints — sink, source, storage and
events, two ends each — plus its own two, the devices, and a child's process
handle. That looked exactly like the cause. It is now **31**
(`sw/kernel/handle.h`) **and the symptom is unchanged.** The change is right on
its own merits and should stay; it is not the bug.

**Where to look next.** Whatever else makes `kernel_port_receive_prepare`
return a status that `port_status_to_syscall` maps to `RESOURCE_LIMIT` when the
message carries one attached handle. Two candidates are not yet eliminated:

- `kernel_handle_import_reserve` in `sw/kernel/handle.c`;
- the detached-handle pool — `KERNEL_HANDLE_DETACHED_MAX` and
  `transfer_stats.reserved_detached`. Note that reservation accounting is
  global rather than per process.

The receive path is `sw/kernel/port.c`; the caller is
`astra_vfs_port_service_pump` in `sw/userspace/vfs/src/vfs_port_transport.c`.

**The fastest way to bisect it** is a kernel unit test rather than the machine.
`sw/kernel/tests/test_process.c` can create a port, send a message carrying an
attached handle, and receive it in the same process. If that passes, the
difference is that the sender is a *different* process — which points at the
transfer and detach path rather than at the receive.

**Scaffolding to delete once it is fixed:** the `probe` lines in `events.c` and
the `[events served/refused/stalled` line in `command_launch`. Keep the
`stalled` field itself — a pump that silently stops receiving is
indistinguishable from one nobody is calling, and that ambiguity is what made
this expensive.

**What is on the branch and should survive regardless of the bug:**

- The stream `INFO` message and `astra_stream_size`, with tests. A pager asks
  how tall the screen is instead of assuming 80x24; this machine is 90x30.
- The port transport **bounds its wait for a reply**. A wait with no deadline
  is the hang task 5's step 1 said it must not have; only the send side had
  been covered, and this bug is what proved it.
- The transport tells a full port apart from a dead peer, instead of calling
  everything `PEER_DEAD`.
- The events drain no longer shares a health flag with the service's ability to
  answer clients. It did, which would have stopped a service because its own
  logging failed — the exact failure that file's header warns about.
- `KERNEL_HANDLE_MAX_ENTRIES` 16 → 31, and the table-exhaustion test now
  derives its fill from the real per-process limits (`KERNEL_SYNC_OWNER_MAX`,
  `KERNEL_PORT_OWNER_MAX`, `KERNEL_PROCESS_THREAD_MAX`) instead of assuming a
  table size.

---

## 3. The mechanisms, as they now stand

### 3.1 A launch

```
ASTRA_SYSCALL_PROCESS_CREATE (48)
  data[1] image address, in the caller's memory   data[2] length
  data[3] AstraLaunchGrant array                  data[4] count
  data[5] AstraLaunchArguments block, or 0
returns
  data[1] a process handle: QUERY | WAIT | TERMINATE, never DEBUG
  data[2] the new process id
```

- **A launch creates no authority.** Every grant names a handle the caller
  holds, with rights that are a subset. A handle it does not hold is
  `INVALID_HANDLE`; rights wider than its own are `ACCESS_DENIED`. Two
  different mistakes, told apart on purpose.
- `ASTRA_LAUNCH_GRANT_MAX` is **6** and `launch_grants` uses all six. **`SYS:`
  is the one left out**, deliberately: a command needs somewhere to read its
  own data, somewhere to write, and its history — not the whole volume.
- The image is copied three times; the launch spec's §1.4 says why each one
  exists and how they go away (a page cache and file-backed mapping).
- `kernel_elf_accept_windowed` bounds the headers to the bytes actually handed
  over, and every segment page bounces through one page of kernel memory. A
  launched image is never read through a user pointer.

### 3.2 What a grant says

`AstraLaunchGrant` carries a name, a handle, `rights` and `flags`, and the last
two are **different vocabularies that cannot share a word**:

- `rights` is what the **kernel** enforces on the handle. A port send endpoint
  carries `READ | SIGNAL | WAIT | TRANSFER` and nothing else, so a grant asking
  for `ASTRA_RIGHT_WRITE` on one is refused — correctly, because there is no
  such authority to give.
- `flags` is what the grant is **for**, carried by the kernel and never read by
  it: `ASTRA_CAPABILITY_FLAG_NAMESPACE`, `_READ`, `_WRITE`. Unknown bits are
  `INVALID_ARGUMENT` at the syscall, so the field stays versionable.

`astra_assign_seed` binds **only** grants carrying `_NAMESPACE`, and takes the
mount's rights from `_READ`/`_WRITE`. The rule is positive: a capability is not
a name unless somebody declared it one, so `PROCESS`, `THREAD` and `STDOUT` are
excluded by construction rather than by a list that grows.

### 3.3 What a child does on the other side

```c
astra_assign_seed(&assigns, capabilities, startup->capability_count);
assign = astra_assign_lookup(&assigns, "EVENTS");
astra_vfs_connect(&client, astra_vfs_port_transport, &assign->handle);
```

From there it is the same Kit the shell uses, unchanged — which is what the
transport being a callback has been for since the protocol was written.
`sw/userspace/commands/events/events.c` is the worked example.

### 3.4 Streams

`astra_stream_write(handle, bytes, length, &written)`, `astra_print(handle,
text)`, `astra_stream_read(source, bytes, capacity, &length)`,
`astra_stream_size(handle, &columns, &rows)`.

- **Every one takes a handle.** There is no ambient output; a print with no
  handle is descriptor 1 with the serial numbers filed off.
- The **port is the queue**. Back pressure is the kernel's `WOULD_BLOCK`, and
  `astra_stream_write` reports exactly how much arrived, which is what makes it
  lossless — a caller retries from there rather than from a guess.
- `astra_stream_size` answering zero and zero is a **successful** answer meaning
  "no geometry" — a file has none — so a redirected program simply does not
  page.

### 3.5 Provenance

`ASTRA_PROGRAM(name, major, minor, patch, author, copyright)` emits one
120-byte record into `.astra_program`. **No record, no link.** The record is
loaded and survives the strip, so `python3 tools/program_info.py <any Astra
ELF>` works on the image as installed. `make link-contract` in the runtime is
the check, and it runs inside `make all`.

Every new program in `sw/userspace/commands/` needs one `ASTRA_PROGRAM` line or
it will not link. That is the point, and it is also the first error a new
command hits.

### 3.6 The serving wait

The services are in the supervisor's process, so **waiting for a child means
serving it**. `pump_once` in `console_shell.c` is one function called from two
places — the prompt's loop and the wait for a child — because a serving wait
that pumped a subset of what the prompt pumps is a child that works until it
calls the one service the wait forgot.

A line typed while a child runs goes to `console_stream_offer` rather than to
`run_line`, **with the newline the person pressed**. Without it an empty line
offers nothing at all and a child waiting for input never sees that return was
pressed. If the source will not take the line the editor is not committed, so
it stays on screen and nothing typed is lost.

---

## 4. Traps that have each cost real time

In addition to the ones in `CLAUDE.md`, which all still apply.

- **Attaching a handle to a port message *moves* it.** The sender's entry is
  invalidated, so a reply handle cannot be cached across calls — every reply
  port is created per call. A first client cached one and passed its tests,
  because the mock **copied** where the kernel **moves**. The mocks in
  `test_streams.c` and `test_vfs_port.c` now model the move, and a test asserts
  that they do.
- **Installing a handle cloneable lets it be *copied*.** A grant is a copy and
  a reply channel is a move: different machinery, same-looking code.
  `kernel_port_handle_retain` exists now and **only the send endpoint is
  cloneable** — a second receive handle would be a second service on one port,
  with messages going to whichever end asked first, so granting one is
  `ACCESS_DENIED`.
- **Two constants for one limit.** `ASTRA_LAUNCH_GRANT_MAX` (6) and
  `KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX` (4) disagreed, so a launch of more
  than four grants failed with `INVALID_ARGUMENT` from inside the loader, which
  names neither the grant nor the reason. It stayed latent for four tasks. They
  are one number with a `_Static_assert` now — **look for the others.**
- **The supervisor must never reach its own services through a port.** It is
  the only thing that pumps them, so a client here waiting on a reply would be
  waiting for itself. `astra_vfs_local_transport` stays for exactly this, and
  the deadlock is one line of code away at all times.
- **A status the shell prints must carry its number.** "would not start" and
  "I/O error" each cost a round trip before they were made to say which status,
  and how far a read had got. Diagnosis time is the thing being optimised.
- `report_status`'s table in `console_shell.c` is indexed by status. It has the
  sixteen protocol statuses plus `ASTRA_STATUS_PEER_DEAD`; anything added to
  `astra/status.h` below 32 needs a name here or prints as a bare number.

---

## 5. Working on this machine

Ship and build from the repo root, and **always rebuild the boot image after
userspace** — the ROM carries the user image, so a rebuilt supervisor that is
not re-ROMmed boots the previous one.

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
    sw/boot/astra_boot.bin --image /tmp/part.img --verbose
python3 emu/qemu/test-events.py  ... same arguments
python3 emu/qemu/time-boot.py    ... --runs 5 --budget 1.0
```

`--verbose` on the terminal gate prints each line as it passes and dumps the
screen on failure. It is the fastest debugging surface the machine has.

On the Mac: `python3 -m pytest tools/tests/` (38 cases) and
`python3 -m pytest sw/boot/tests/`. **Reap QEMU** after every gate —
`pkill -f qemu-system-m68k` — because a lingering emulator makes the next gate
look like a machine that will not boot.

Last known-good figures on `main`: supervisor text **92,487**, boot **0.09s**
of a 1.00s budget.

---

## 6. Design authority

| Question | File |
|---|---|
| Launch, grants, streams, command lookup | `docs/superpowers/specs/2026-08-07-program-launch-design.md` |
| The three image copies, and the way out of them | the same, §1.4 |
| Lookup order, what a file is, provenance | `docs/superpowers/specs/2026-08-06-filesystem-layout-design.md` §2.5, §11 |
| Union assigns (milestone 1.5, not this one) | the same, §1.7 |
| The plan, and every "what the build settled" | `docs/superpowers/plans/2026-08-07-launch-milestone-1.md` |

## 7. After this milestone

Two things are queued and neither is started:

- **Milestone 1.5: union assigns**, approved 2026-08-07, layout spec §1.7.
- **The events store is RAM**, so `EVENTS:boot/-1` does not exist. Durability
  is the next thing after 1.5, and the terminal gate already asserts the
  refusal so nobody mistakes it for a missing feature.
