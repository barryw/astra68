# Milestone 1: a program runs, and `events` is one

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `COMMANDS:events` is a file on the volume, built from its own source,
launched by name from the prompt, printing to the terminal through a stream it
was granted. The shell's `events` builtin is deleted in the same commit that
makes the file work.

**Architecture:** Four mechanisms, in the order each unblocks the next.

1. **A launch syscall.** The kernel already loads an ELF; this is the door and
   the rule that a launch creates no authority.
2. **Streams.** A launched program with nowhere to write is a program nobody can
   use. `STDOUT`, `STDERR` and `STDIN` are grants, not numbers.
3. **The port transport.** `events` reads `EVENTS:`, which is a service in
   another process now. The Kit's transport callback was written for this day.
4. **The migration.** `events` becomes a program, `COMMANDS:` gets a binding,
   and the builtin goes.

Every one of them replaces a comment already in the tree that says "when a
loader exists".

**Design authority:** `docs/superpowers/specs/2026-08-07-program-launch-design.md`,
and `2026-08-06-filesystem-layout-design.md` §2.5 for lookup.

**Tech Stack:** C11. Kernel work in task 1, so every gate is Beast.

## Global Constraints

- **A launch creates no authority.** A grant the caller does not hold, or rights
  wider than it holds, fails the call — never a silent narrowing, never a skip.
- **All or nothing.** A failed launch leaves no process, no frames, no mappings
  and no handles. The existing loader guarantees this and nothing here may
  weaken it.
- **The supervisor serves while it waits.** The services are in its process and
  the shell is its only loop, so waiting for a child means serving that child.
  A wait that stops serving is a deadlock, and it is the deadlock this
  architecture makes easy to write.
- **Bounded everywhere.** One launch at a time, one load buffer with a written
  ceiling, one bounded message per stream write.

## Where this milestone stops short, on purpose

**One catalog.** The events store resolves message ids against the supervisor's
catalog. A launched program has its own `.astra_events` section, so *its* events
render as ids until the store keeps a catalog per image — which is what the
events spec's §3 and the process-start event of the launch spec's §2 are for.
`events` mostly reads rather than emits, so this is visible and small.
**Trigger:** the second program that emits events worth reading.

**No page cache, so an image is copied three times.** The launch spec's §1.4
now has the count, the reason for each one and the way out: file-backed mapping,
where a program's read-only text *is* the cached frames and a second instance
costs no text at all. That is the fix worth having and it is a subsystem, not a
tweak. **Trigger:** launch latency showing up in a measurement, or the second
concurrent instance of one program.

**No redirection, no pipes, no job control.** A stream is a capability, so
redirection is a different grant rather than a new mechanism; what is missing is
a shell language to spell it, which is its own spec.

**No `PATH`, and no environment.** Layout §2.5 and launch §5: two places, one
stated order, and subdirectories that are never searched. See the note at the
end of this plan.

**Services stay in the supervisor's process.** This milestone gives them a port
transport, not a launch. Moving them out is the next one, and it is smaller once
their clients already speak ports.

---

### Task 1: The launch syscall

**Files:** `sw/include/astra/syscall.h`, `sw/include/astra/process.h`,
`sw/kernel/process.c`, `sw/kernel/tests/test_process.c`

`ASTRA_SYSCALL_PROCESS_CREATE` (48), taking an image, a length, an
`AstraLaunchGrant` array and a count; returning a process handle carrying
`QUERY | WAIT | TERMINATE` and the new process id.

The handler validates every grant against the caller's own handle table before
anything is allocated: the handle must be held, and the rights must be a subset.
Then the existing `kernel_process_create_executable` does the work it already
does.

- [x] Step 1: failing tests — a launch from a valid image creates a runnable
      child; a grant naming a handle the caller does not hold is
      `INVALID_HANDLE`; rights wider than the caller's are `ACCESS_DENIED`; a
      malformed image is refused with no process left behind; the returned
      handle answers `PROCESS_INFO` and refuses `TRACE_READ`.
- [x] Step 2: the syscall.
- [x] Step 3: `cd sw/kernel && make test` on Beast, both configurations.
- [x] Step 4: commit.

**Three things the build settled.**

- **The image is in the launcher's memory, so the kernel may not read it
  directly.** The headers arrive through a bounded window —
  `kernel_elf_accept_windowed`, which refuses a program header table outside
  the bytes it was actually handed — and every page bounces through one page of
  kernel memory. That is the third copy of a launched image, and it is the one
  bounded rather than proportional to the program.
- **Only a cloneable object can be granted.** A copy needs a retain, and areas,
  IRQ endpoints and devices have one. **Ports do not**, so task 5 must give the
  port endpoints a retain before a service handle can be what a child is
  handed. Found here rather than there, which is the cheap place to find it.
- **The child's argv is published into its startup page**, after the capability
  table, as a vector of addresses and then the bytes. Task 4 needs it and the
  ABI already had `argc`/`argv_address`, so it landed with the syscall rather
  than after it.

### Task 2: The runtime's half

**Files:** `sw/userspace/runtime/include/astra/runtime.h`,
`sw/userspace/runtime/src/launch.c`, `sw/userspace/runtime/tests/test_runtime.c`

`astra_launch(image, length, grants, count, &process_handle, &process_id)` and
`astra_process_wait(handle, &status)` — the second is the existing wait, named
for what a launcher does with it.

Also the other end: a launched program's `astra_main` receives a startup block
whose capability table is its namespace, and the runtime already validates it.
What is new is seeding an `AstraAssignTable` from that table, so a child gets
`WORK:` and `EVENTS:` as names rather than as handles it has to interpret.

- [x] Step 1: failing tests against the syscall mock — the wrappers marshal what
      the ABI says; an assign table seeded from a capability table has the
      names, roots and rights the table carried.
- [x] Step 2: the wrappers and the seeding.
- [x] Step 3: `cd sw/userspace && make test && make sanitize && make analyze`.
- [x] Step 4: commit.

**Three things the build settled.**

- **`astra_process_wait` takes a deadline**, which the plan's signature did not.
  A deadline of zero polls, and polling is the only form the supervisor may use:
  it hosts the services its child is calling, so a wait that blocks is the
  deadlock in the global constraints. A wrapper task 4 has to bypass is not a
  wrapper.
- **Both wrappers clear their outputs before anything else**, including before
  the refusal. A launcher that reads a handle out of a launch that did not
  happen closes a handle belonging to something else, and that fault surfaces a
  long way from here. `exit_status` is published only when the wait established
  one — a clean exit or a fault — so a zero from a timed-out poll is the absence
  of an answer rather than an answer.
- **Seeding skips `PROCESS` and `THREAD` by name.** They are what the kernel
  installs for every process whether it asked or not, and they are authority
  over itself rather than names in a namespace. Everything else the table
  carries is bound, and an entry the namespace cannot take — no rights, a name
  nobody could type — is skipped rather than fatal, because the capability table
  is where *every* kind of authority is published and one odd entry must not
  cost a child the names it was given. Running out of room is the one thing
  reported. The flag that would say "this grant is a mount" is the same deferral
  as the grant's root: added when the first grant needs one, which is task 3's
  `STDOUT`.

### Task 2b: `ASTRA_PROGRAM`, and the link that fails without it

**Files:** `sw/include/astra/program.h` (new),
`sw/userspace/runtime/astra_user.ld`, `sw/userspace/supervisor/src/main.c`,
`tools/program_info.py` (new), `tools/tests/`

Layout spec §11.2: every image declares its name, semantic version, build id,
author and copyright, in one fixed record in `.astra_program`, and the linker
script asserts there is exactly one. The macro is `ASTRA_EVENT`'s shape and the
extraction is `event_catalog.py`'s.

The supervisor declares one too — it is the first image on the machine and the
rule has no exceptions, which is the only way a rule about every program
survives the first program that finds it inconvenient.

- [x] Step 1: the record, the macro, the linker assertion, and a host test that
      an image missing it does not link.
- [x] Step 2: `tools/program_info.py` reads it out of an ELF, with a pytest
      case, so a person can ask what a file is before running it.
- [x] Step 3: the userspace gate and the cross-build on Beast.
- [x] Step 4: commit.

**Three things the build settled.**

- **The record is loaded, unlike the event catalog beside it.** Layout §11.4 is
  the reason: truth about a file is in the file, so a `version` command has to
  read the provenance off the image as *installed*, not off an unstripped build
  in somebody's tree. `.astra_program` sits at the head of the rodata segment
  and survives the `objcopy` that strips `.astra_events`. It costs the ROM 120
  bytes: 88,099 to 88,219 of text, which is the record and nothing else.
- **Two mechanisms, because they catch different mistakes.** The linker's
  `ASSERT` refuses an image with no record. A *second* record is caught earlier
  and far more usefully by the duplicate `astra_program` symbol, which names
  both files — the macro's object is deliberately not static for that reason.
  A string too long for its field is a `_Static_assert`, not a truncation: a cut
  copyright notice is a legal statement nobody made.
- **The link contract is a make target, not a host test.** `make link-contract`
  in the runtime links `tests/link_contract_with.c` and
  `tests/link_contract_without.c` against the real `astra_user.ld` with
  `--gc-sections` on — the collector is the whole reason the section needs
  `KEEP`, and a check that dropped the flag would pass while the real build
  shipped no record. It then reads the linked image back with
  `tools/program_info.py`, which is the only place anything checks that the C
  struct and the Python parser agree; the tool's own pytest builds its fixture
  by hand, which pins the format but cannot notice the compiler laying the
  struct out differently. It runs as part of `make all`, because that is the
  step that already needs a cross toolchain.

**`build_id` is zero and says so.** The field is in the record and
`ASTRA_BUILD_ID` overrides it, but nothing defines one yet: the events spec's
process-start event is what needs a build id to be real, and a number that
looks like provenance and is not would be worse than an honest zero.
**Trigger:** the process-start event, or the second catalog.

### Task 3: Streams

**Files:** `sw/include/astra/stream_service.h` (new),
`sw/userspace/streams/` (new: the sink and the client),
`sw/userspace/supervisor/src/console_stream.c` (new), and their tests.

The protocol is deliberately smaller than the storage one: a bounded text write
with no reply, and a bounded read request with one.

```c
#define ASTRA_STREAM_WRITE_MAX 192u   /* one message, inline */

typedef struct AstraStreamWrite {
    uint16_t size;
    uint16_t length;         /* bytes that follow */
    uint32_t activity;       /* the story this line belongs to */
    uint8_t  bytes[ASTRA_STREAM_WRITE_MAX];
} AstraStreamWrite;
```

- The supervisor hosts the sink: a port whose messages it renders into the
  terminal model it already owns. `STDOUT` and `STDERR` are two send handles to
  it, granted separately so they can later point at different things.
- `STDIN` is a request and a reply over the same shape: the child asks for at
  most N bytes, and gets what the line editor has finished with. A short read is
  ordinary.
- **Back pressure is `WOULD_BLOCK`, and a writer retries.** There is no reply to
  a write, because a reply per line doubles the round trips at 30 MHz and
  nothing depends on the sink's opinion.
- `astra_stream_write(handle, bytes, length)` and `astra_stream_read(...)` in
  the runtime; `astra_print(text)` over the first, because every program will
  want it.

- [x] Step 1: failing tests — a write renders into a terminal model; a write
      longer than one message is refused rather than truncated, and the client
      loops instead; a full sink answers `WOULD_BLOCK` and loses nothing; a read
      with nothing available is short rather than an error; a stream a program
      was not granted is a handle it does not have.
- [x] Step 2: the sink, the client and the runtime wrappers.
- [x] Step 3: `cd sw/userspace && make test && make sanitize && make analyze`.
- [x] Step 4: commit.

**The grant flag, settled first.** Seeding bound every published capability that
was not `PROCESS` or `THREAD`, so a `STDOUT` grant would have been bound as
though `STDOUT:src/main.c` meant something. `AstraLaunchGrant.flags` existed,
unused and not propagated. It is now carried through
`KernelProcessBootstrapCapability` into the published record, unknown bits are
refused at the syscall, and `astra_assign_seed` binds only
`ASTRA_CAPABILITY_FLAG_NAMESPACE`. **The rule is positive**: a capability is not
a name unless somebody declared it one, so `PROCESS` and `THREAD` are excluded
by construction rather than by a list that grows every time a new kind of
capability is invented. Four lines of kernel and the blacklist is gone.

**Four things the build settled.**

- **Attaching a handle to a port message *moves* it.** Ports carry no retain, so
  they go through the transfer machinery: the sender's entry is invalidated. A
  first client cached one reply port across reads, passed its tests, and would
  have named nothing on the second read on the machine — the mock copied where
  the kernel moves. `astra_stream_read` now creates a reply port per call, and
  **the mock models the move**, with a test that asserts it does. This is the
  same finding as task 1's "ports are not cloneable", from the other direction.
- **The port is the queue, so the sink has none.** Back pressure is the kernel's
  `WOULD_BLOCK` on a full port, and `astra_stream_write` reports exactly how
  much went — which is what makes it lossless, because a caller retries from
  there rather than from a guess. A buffer in the sink would have been a second
  place to drop a message.
- **A reader blocks on its reply port rather than yielding at it.** A yield loop
  burns a scheduling slot per pass for as long as somebody takes to type, and on
  a machine whose shell is also the thing that has to answer, that is time taken
  from the answer. The host test's `astra_wait_one` pumps the source, which is
  not a convenience: it is exactly the serving wait the machine does.
- **`astra_print` takes a handle**, unlike the plan's `astra_print(text)`. A
  print with no handle is descriptor 1 with the serial numbers filed off, and
  ambient output is what 4.1 refuses. The protocol also rides `AstraMessageHeader`
  rather than the plan's bare struct, because the kernel refuses a port message
  without a valid one.

**`STDIN` is wired but not fed.** `console_stream_offer` is the seam and nothing
calls it yet: what the line editor has finished with only becomes a child's
input once there is a child, and the shell's serving wait is task 4. The source,
its protocol and its tests are done, so task 4 adds a call rather than a
mechanism.

The supervisor grew 1,328 bytes of text and 732 of BSS: two ports, a sink, a
source and the client.

### Task 4: The shell launches by name

**Files:** `sw/userspace/supervisor/src/console_shell.c`,
`sw/userspace/supervisor/src/vfs_host.c`, `emu/qemu/astra_image.py`,
`sw/userspace/commands/` (new, with a first program)

- `COMMANDS:` binds to `commands/` on the volume, beside `work/`, with read
  rights — the same shape `WORK:` already has.
- A word the shell does not recognise as a builtin becomes a launch: resolve
  `APPS:` then `COMMANDS:` (layout §2.5, top level only), open, read into the
  load buffer, `astra_launch` with the grants the line implies, wait, report.
- **The wait serves.** While the child runs, the loop pumps the stream sink, the
  events drain and the storage service, and checks for the child's death. That
  is the whole reason the shell's loop already looks the way it does.
- The first program is `sw/userspace/commands/status`: it exits with the number
  it was given, and prints nothing. It exists to prove the launch, the argument
  vector and the status path without needing a stream.

The gate's image preparation grows a second job beside the catalog: place the
built commands into `commands/` on the volume copy.

- [ ] Step 1: the binding, the launch path, the serving wait, and `status`.
- [ ] Step 2: `emu/qemu/astra_image.py` installs `COMMANDS:` contents.
- [ ] Step 3: the terminal gate — `status 7` reports 7, `status` with no
      argument reports 0, and a name that is not there says so rather than
      hanging.
- [ ] Step 4: the whole gate on Beast, then commit.

### Task 5: The storage protocol over ports

**Files:** `sw/userspace/vfs/src/vfs_port_transport.c` (new),
`sw/userspace/supervisor/src/vfs_host.c`,
`sw/userspace/supervisor/src/events_host.c`, and their tests.

The client side is a transport callback that sends a request and waits for the
reply; the service side is a receive-dispatch-reply loop the supervisor runs
from the same place it pumps everything else. `astra_vfs_local_transport` stays,
because the host tests are built on it and a service in the same process should
not pay for a port it does not need.

A child is granted a send handle per mount, and its assign table's handles are
those. The router in `vfs_host.c` is deleted rather than extended.

**A service adopts the caller's activity** here — `astra_activity_adopt` has
existed unused since the activity landed, and this is the boundary it was
written for.

- [ ] Step 1: failing tests — a request crosses a port and the reply matches
      what the local transport would have produced; a reply to a dead peer is
      `PEER_DEAD` and not a hang; an activity set by the caller is the service's
      activity while it handles the request and is restored after.
- [ ] Step 2: the transport, the serve loop, the grants.
- [ ] Step 3: `cd sw/userspace && make test && make sanitize && make analyze`,
      then both QEMU gates.
- [ ] Step 4: commit.

### Task 6: `events` becomes a program

**Files:** `sw/userspace/commands/events/` (new),
`sw/userspace/supervisor/src/console_shell.c` (the builtin, deleted)

The command moves nearly as-is: it is already a path builder and a pager over
`EVENTS:`, and what it loses is the shell's terminal and the shell's client. It
gains `astra_print` and an assign table seeded from its own capability grants.

The screen height it pages to comes from the stream: a `STDOUT` that is a
terminal answers how wide and how tall it is. That is one more message on a
protocol that already exists, and the alternative — a program that assumes 80x24
— is the thing every terminal program on every other machine gets wrong.

**Deleting the builtin is part of this commit.** Two implementations of one
command is exactly the drift this project refuses, and the gate proves the file
works before the builtin goes.

- [ ] Step 1: the program, built and linked like the supervisor is.
- [ ] Step 2: the builtin deleted; `help` names the file, not the builtin.
- [ ] Step 3: the terminal gate — `events` and `events --subsystem shell
      --level warning` produce what they produced as a builtin, from a file in
      `COMMANDS:`.
- [ ] Step 4: the whole gate on Beast, commit, and update the handover.

---

## What this milestone deliberately does not do

No pipes, no redirection, no background jobs, no `PATH`, no environment, no
shared libraries, no POSIX personality. Every one of them is a spec of its own,
and every one of them is cheaper to write once a program can run at all.

## The `PATH` question, recorded

`PATH` was raised as the way `COMMANDS:` should be found, on the strength of
experience with machines that work that way. It is not in this plan, and the
reason is in the launch spec's §5 and the layout spec's §2.5: a search path is a
hijacking surface, an unanswerable "which one ran", and a scan per invocation on
a 30 MHz machine.

What `PATH` is actually for — reaching a program that is not where the machine
looks — already has an answer here: **an assign**. Binding `TEXT:` to
`COMMANDS:text` makes `TEXT:wc` a name a person can type, checked by rights,
resolved without a search, and answerable about which one ran. That is one
mechanism rather than two, and it is the mechanism the machine already has.

If an environment is wanted for its own sake — and it is a reasonable thing to
want for *ported* software, where `getenv` is table stakes — it belongs to the
POSIX personality, which can synthesise one from its own configuration without
the native side growing ambient inherited state. That keeps the native rule
("nothing is inherited implicitly") intact where it earns its keep.
