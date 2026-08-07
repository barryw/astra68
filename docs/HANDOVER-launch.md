# Astra 68 — Handover: milestone 1, and the one thing left in it

Date: 2026-08-07. Written to be read cold in a fresh session. Read `CLAUDE.md`
first; this is the continuation map for the launch milestone only.

**A program runs from the prompt.** `status 7` is a file on the volume, found
by name, loaded, run, and its exit status reported. A launched child is handed
six capabilities: `STDOUT`, `STDERR`, `STDIN`, `WORK:`, `COMMANDS:` and
`EVENTS:` — the last three as port handles to services it reaches across a
process boundary.

**Task 6 works. One fault is left and it is not task 6's.** `events` is a
program, it is granted `EVENTS:`, and the terminal gate reaches 20 of 20. But
the gate is **flaky**: roughly half of runs die at the seventh launch of a
program because the block device's interrupt endpoint quarantines itself and
never recovers. §2 is that fault, and it is where to start.

---

## 1. Where the code is

| Where | What | State |
|---|---|---|
| `main` | tasks 1–5 | **pushed**, `ec75bb4`. Every gate green. |
| `task6-events-program` | task 6 | `2c14a0a`. Gate green when it passes; **flaky**, §2. |

`main` was pushed to `origin` at `435d530..ec75bb4`, 48 commits. Task 6 is still
on a branch because of §2, not because of task 6.

Every gate below is green on the branch: kernel `make test` and
`K1_QUALIFICATION=1`, userspace `make test`, `sanitize` and `analyze`,
`ext4-test`, and `tools/tests` (38 cases). The terminal gate passes whole when
§2 does not fire.

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
| 6 | `events` becomes `COMMANDS:events`; the builtin goes | done, `2c14a0a` |

---

## 2. Resume here: the interrupt endpoint that quarantines itself

Run the terminal gate two or three times. Roughly half the runs die like this,
at `events --follow` — the seventh program launched in the run:

```
WORK:> events --follow
events: read stopped at 9600 of 15564, device 9/1292
events: I/O error
```

That is the **shell** failing to read the program's image off the volume, not
`events` failing. After it happens, storage never works again for the rest of
the boot: the next read stops at byte 0.

**Read the numbers.** `device 9/1292` is new and is the whole of the progress
made. `9` is `ASTRA_BLOCK_IO_ERROR`. `1292` is `site * 256 + status`, so site
**5** = `ASTRA_LEASE_BLOCK_SITE_DRAIN_AFTER_COLLECT` and status **12** =
`ASTRA_SYSCALL_IO_ERROR`. See `AstraLeaseBlockSite` in
`sw/userspace/storage/include/astra/lease_block.h`.

So: a request was submitted and **collected successfully**, and then the drain
that acknowledges its completion record was refused with `IO_ERROR`.

**Why it never recovers.** `kernel_irq_read` (`sw/kernel/irq.c`) returns
`KERNEL_IRQ_DEVICE_ERROR` whenever the endpoint holds no records *and* the
`KERNEL_IRQ_EVENT_DEVICE_ERROR` flag is set. **That flag is sticky.** Once set,
every later drain fails, `run_request` turns it into `ASTRA_BLOCK_IO_ERROR`,
lwext4 turns that into `EIO`, and the volume is dead until reboot.

**What is not yet known: which of three paths sets the flag.**

- `kernel_irq_ack` sets it when the route's `complete` callback returns false;
- `kernel_irq_ack` sets it when a level-triggered `controller_acknowledge`
  fails;
- `quarantine_masked` / `quarantine_unclaimed` set it when an interrupt arrives
  while the endpoint is masked or unowned.

**The next step is already built.** `trace_quarantine` in `sw/kernel/irq.c`
emits a trace record naming the reason, and `emu/qemu/test-events.py` takes the
ring off the machine and decodes it — it works even when storage is dead,
because the ring is RAM. Run the terminal gate until it fails, pull the ring,
and read which quarantine fired. That names the path in one run.

**Why it is timing-dependent, and why launching is what exposes it.** A launch
runs another process between the shell's block requests, so completions land at
different points relative to arm/ack than they do when only the shell runs. The
onset moves between the fifth and seventh launch across runs. Nothing about
`events` matters here: 12 consecutive `status` launches fail the same way.

**Do not "fix" this by clearing the flag.** Sticky is the right behaviour for a
device that misbehaved; the bug is whatever makes a healthy device trip it. The
lease's arm/ack state machine and the kernel's are two models of one endpoint
(`lease->armed` in `sw/userspace/storage/src/lease_block.c`), and they are the
first thing to check for divergence.

**What was fixed getting here** — all of it committed, and none of it the
above:

- The original refusal. `ASTRA_SYSCALL_PORT_RECEIVE` answers `RESOURCE_LIMIT`
  from exactly one place, `KERNEL_HANDLE_TABLE_FULL`. The supervisor's table
  was full at 16 entries. The previous session's widening to 31 was correct and
  **had never compiled** — a `_Static_assert` on `sizeof(KernelProcess)` still
  named the old size — so every observation behind "the symptom is unchanged"
  was made against a 16-entry ROM.
- `KERNEL_PROCESS_HANDLE_DEMAND` in `sw/kernel/process.h` now sums what one
  process can hold and asserts it fits the table. At 16 it comes to 27 and the
  build fails naming the budget.
- `pump_once` painted a child's output only on a pass where a key arrived, so a
  program that printed one line and exited printed nothing until somebody
  typed.
- `command_launch` reported a child's exit above the child's last words; it
  drains the sink before it narrates.
- The port transport answered `ASTRA_VFS_ERR_NOT_FOUND` when a service had
  died. `test_vfs_port` had been failing on this branch and now asserts a dead
  peer and a bad handle apart, rather than together.

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

**There are two kernels, and `make` builds the shipping one.**

```sh
make                      # release: quarantines, faults and the boot
make ASTRA_BUILD=debug    # and every interrupt delivered and acknowledged
```

A site declares the level it writes at and the build decides; anything below
the floor is compiled out, so a release pays no ROM and no cycles for it. On a
run of the terminal gate that is **41 ring records against 1,639** -- the
per-interrupt stream is two records per transferred sector, which fills a
64 KiB ring in seconds and is exactly what you want when a device is
misbehaving. Reach for `ASTRA_BUILD=debug` then, and read the ring with
`emu/qemu/irq_quarantine_probe.py` or the dump the terminal gate now prints on
any failure.

`ASTRA_BUILD` must be given to whichever make you run: `sw/boot` rebuilds the
kernel, so `make -C sw/boot ASTRA_BUILD=debug` is what produces a debug ROM.
Building the kernel debug and then the ROM without it silently ships a release
kernel.

The gates, all on Beast:

```sh
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1
# and the host suite at the other floor, so the debug build cannot rot:
cd sw/kernel && make test \
    HOST_EXTRA_FLAGS="-DKERNEL_TRACE_BUILD_LEVEL=KERNEL_TRACE_LEVEL_DEBUG"
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

**`make all` in `sw/userspace` does not build `commands/`.** It builds the
libraries and the supervisor, and `events` and `status` keep whatever they were.
The gate installs those files onto the volume at every run, so a stale one is
invisible: you are debugging a binary you did not build. `cd sw/userspace/commands
&& make` is a separate step, and `strings build/m68k/events | grep ...` is how
to be sure. This cost a full run and a wrong conclusion.

**Keep a clean volume.** `/tmp/part.img` is written by every run and the gate
kills QEMU rather than shutting it down, so unclean mounts accumulate until
`mkdir` starts answering `I/O error` and the screen renders as garbage. That is
not a filesystem bug, it is the image. Repair and snapshot once:

```sh
dd if=/tmp/part.img of=/tmp/vol.img bs=512 skip=10240 count=120832
e2fsck -fy /tmp/vol.img
dd if=/tmp/vol.img of=/tmp/part.img bs=512 seek=10240 conv=notrunc
cp /tmp/part.img /tmp/part-clean.img
```

then `cp /tmp/part-clean.img /tmp/part.img` before every run. §2 is flaky and
you will be running the gate repeatedly; without this the two failure modes are
easy to confuse.

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
