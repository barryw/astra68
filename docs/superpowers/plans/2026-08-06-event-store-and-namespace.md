# Event Store and Namespace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Plan 5 of six. The events the machine already emits become history a
person can read on the machine, without a new protocol and without a new client:
`cat EVENTS:activity/0000001a` prints one request across every process that
touched it.

**Architecture:** `EVENTS:` is a **synthetic tree**, served through
`AstraVfsBackendOps` — the same seam `vfs_ext4_backend.c` sits behind. The
service core, the wire format, the Kit and the shell do not learn that a second
kind of filesystem exists; they learn that there is a second mount.

That choice is the reason this plan is smaller than the bespoke query service
the spec first described. It buys `ls`, `cat`, redirection and every future
reader for the cost of one backend, and it is what `docs/OBSERVABILITY.md`
already required to be possible when it said no special case may be needed to
add a tree beside `PROC:`.

Three pieces have to exist under it:

1. a way for userspace to drain the kernel trace ring — a cursor syscall, gated
   on `ASTRA_RIGHT_DEBUG`, because §6.1 makes reading the privileged half;
2. the catalog **on the machine**, so text can be rendered at read time;
3. `readdir` as a cursor, because the tree has directories with thousands of
   entries and enumeration is quadratic today.

**Tech Stack:** C11. Kernel work in task 1 and userspace after it, so every gate
is Beast. The Python halves run on the Mac.

## Global Constraints

- Design authority: `2026-08-06-event-system-design.md` §7 and §8, and
  `2026-08-06-filesystem-layout-design.md` §2.3, §3.2 and §6.
- **No parser on the machine.** The catalog is the `.astra_events` section's
  bytes verbatim and lookup is `(id - base) / 128`. An index, not a parse.
- **Logging may never cause the failure** (§8.5). Emission stays a copy into the
  ring; the drain is off the critical path; the events service may not emit an
  event caused by handling an event, and that has to be structural.
- **Nothing renders an unbounded result into memory.** A read returns a bounded
  page; the file's size is not known before the read, which the handler contract
  already allows.
- `EVENTS:` is bound **read-only**. Not by policy inside a command — by the
  rights on the binding, which is what makes "nothing can `rm` an event" true.

## Where this plan stops short, on purpose

**No token bucket per subsystem (§8.4).** The store is bounded and eviction is
accounted, so a runaway subsystem costs eviction and not a full disk. The spec
itself says the rates want a measured workload rather than an opinion, and there
is no workload to measure until the store exists. **Trigger:** the eviction
accounting shows one subsystem evicting another's history.

**No coalescing (§8.3).** Same reason, same accounting, and it is a drain-time
transform that changes no stored record's shape when it arrives. **Trigger:**
repeats dominate an eviction report.

**No `STARTUP:` manifest.** The `serves EVENTS:r` clause is written into the
layout spec, and nothing reads a manifest yet; the mounter binds `EVENTS:` for
itself exactly as it binds `SYS:` and `WORK:` today, with the same `ponytail:`
note pointing at the loader.

**No `events` command.** Plan 6. Until then a path is the whole interface, which
is the point of the shape.

---

### Task 1: The ring is drainable

**Files:** `sw/include/astra/syscall.h`, `sw/kernel/trace.h`, `sw/kernel/trace.c`,
`sw/kernel/process.c`, `sw/kernel/tests/test_process.c`

`ASTRA_SYSCALL_TRACE_READ` (46). A caller passes the sequence it has already
seen and a bounded buffer; the kernel copies the records after it, in order, and
returns how many and the sequence to pass next. Records the caller was too slow
for are reported as a count of lost events, never skipped silently — §6.2.

`kernel_trace_read_user` and `kernel_trace_copy_recent` already exist, so this is
a bounded copy and an authority check rather than new ring machinery.

- **`ASTRA_RIGHT_DEBUG` is required.** Emission stays ungated; this is the read
  half, and it is the one place the whole account of the machine can be
  siphoned from.
- A torn slot is skipped and counted, not returned. `kernel_trace_test_inject_torn_read`
  exists for exactly this test.

- [x] Step 1: failing tests — a caller without `ASTRA_RIGHT_DEBUG` is refused; a
      drain returns records in sequence order; a second drain from the returned
      cursor returns only what is new; overtaken records are reported as loss; a
      torn slot is counted rather than returned.
- [x] Step 2: the syscall and the copy.
- [x] Step 3: `cd sw/kernel && make test` on Beast, both configurations.
- [x] Step 4: commit.

**As built, one thing narrower than this plan said:** the handle must *name the
caller*. Reading is every process's events at once, and borrowing a DEBUG handle
over some third process to obtain that would launder an authority nobody
granted. Only a diagnostic-surface build puts DEBUG on a process's own handle,
so that is the gate today; when the loader exists it becomes the manifest's
`serves EVENTS:r` grant, and the ABI does not change.

### Task 2: `readdir` becomes a cursor

**Files:** `sw/userspace/vfs/include/astra/vfs_backend.h`,
`sw/include/astra/vfs_service.h`, `sw/userspace/vfs/src/vfs_ext4_backend.c`,
`sw/userspace/vfs/src/vfs_service_core.c`, `sw/userspace/vfs/src/vfs_client.c`,
and their tests.

The contract's `uint32_t index` becomes a `uint64_t cookie`: zero starts a scan,
the backend returns the cookie for the next entry, and a returned end-of-scan is
`ASTRA_VFS_ERR_NOT_FOUND` as today. The cookie is backend-private and travels on
the wire, so the protocol stays stateless per request — which was the reason the
contract chose an index, and a cookie keeps that property while dropping the
cost.

The ext4 backend maps it onto lwext4 directly: `ext4_dir.next_off` **is** the
iterator offset, and `ext4_dir_entry_next` resumes from it. Seek, read one,
return the new `next_off`. The comment in `vfs_ext4_backend.c` about a walk per
entry gets deleted along with the walk.

This is the layout spec's §1.7 debt for union assigns, paid here because
`EVENTS:activity` puts it on the critical path.

- [ ] Step 1: failing tests — a full scan visits every entry exactly once; an
      opaque cookie round-trips through the wire record; a cookie from a
      different directory yields no entry rather than a wrong one; a scan of a
      large directory does not re-walk (assert the backend's open count).
- [ ] Step 2: the contract, the ext4 backend, the core, the client.
- [ ] Step 3: `cd sw/userspace && make test && make sanitize && make analyze`,
      then `cd sw/userspace/storage && make ext4-test` on Beast.
- [ ] Step 4: commit.

### Task 3: The catalog on the machine

**Files:** `sw/userspace/runtime/include/astra/event_catalog.h`,
`sw/userspace/runtime/src/event_catalog.c`, the supervisor's makefile,
`tools/event_catalog.py`, `sw/userspace/runtime/tests/test_event_catalog.c`

The file is `objcopy -O binary --only-section=.astra_events` over the image, on
`SYS:`, unmodified. The reader is two things and nothing else:

- **lookup:** `(id - base) / 128` bounds-checked against the file's length. A
  descriptor is fixed-size by construction, which is what makes this an index.
- **render:** walk the format string, substitute the typed arguments in order,
  refuse anything the argument types do not describe. The bounded set from §1.4
  is the whole grammar: `u32`, `u64`, `s32`, `status`, `handle`, one inline
  string.

`tools/event_catalog.py` and `tools/trace_decode.py` stay the off-machine
readers and stay authoritative for the format. The C renderer is checked against
them by rendering the same descriptors from the same bytes and comparing, which
is a test, not a second source of truth.

- [ ] Step 1: failing tests — a known id resolves to its descriptor; an id below
      base, above the end, or misaligned is refused; each argument type renders;
      a format string wanting an argument that is not there renders a marker
      rather than reading past the record.
- [ ] Step 2: the extraction step, the lookup, the renderer.
- [ ] Step 3: `cd sw/userspace && make test && make sanitize && make analyze`.
- [ ] Step 4: commit.

### Task 4: The store

**Files:** `sw/userspace/events/` (new: `event_store.c`, `event_store.h`,
`tests/test_event_store.c`)

The service's own bytes, in the state volume's `events/`, reached through the
ordinary storage client — a service is a client of another service, with no
private path to a disk.

- **Four rings, four budgets** (§8.1): `presented`, `record`, `detail`, and the
  boot ring; `debug` never persists. One number configures the total and the
  fractions are fixed, so no one can produce an incoherent split.
- **The boot ring keeps the first N events of the last M boots** (§8.2),
  outside the tier budgets, because the earliest events are the most valuable
  and a FIFO evicts them first.
- **Eviction is accounted** — what was dropped, from which subsystem — because
  §8.4's numbers are supposed to come from evidence, and this is the evidence.
- A stored record is the §1.1 ring record plus a repeat count and a last-seen
  tick. The counts are written now and stay at one until coalescing exists, so
  the record does not change shape when it does.
- **No state volume means RAM**, bounded the same way, and the machine says so
  rather than looking like a machine that logged nothing (§9.3).

Host-tested: the store takes a client and a clock, both of which the host test
already knows how to fake for the VFS.

- [ ] Step 1: failing tests — a drained record lands in the tier its level
      names; a full tier evicts oldest and counts it; a screaming `info`
      subsystem cannot evict an `error`; the boot ring survives a tier wrap;
      `debug` never lands; a store with no volume still accepts and bounds.
- [ ] Step 2: the store.
- [ ] Step 3: `cd sw/userspace && make test && make sanitize && make analyze`.
- [ ] Step 4: commit.

### Task 5: The events backend

**Files:** `sw/userspace/events/event_backend.c`, `event_backend.h`,
`tests/test_event_backend.c`

`AstraVfsBackendOps` over the store. The tree is §7.1's:

```
EVENTS:
  boot/current/{all,notice,warning,error}   boot/-1/ boot/-2/
  activity/<8 hex digits>
  subsystem/<name>
```

- `open` on a leaf yields a query, not a file: the node token is the filter.
  Nothing is materialised; `info.size` is unknown, which the handler contract
  was written to allow.
- `read` renders forward from the offset, a bounded page at a time, and returns
  short rather than lying about the length. Re-reading a file that grew is what
  `--follow` becomes.
- `readdir` over `activity/` is the cursor from task 2, over the store's own
  ordering. `boot/` and `subsystem/` are small closed sets.
- `write`, `mkdir` and `unlink` return `ASTRA_VFS_ERR_DENIED` unconditionally.
  The rights on the binding refuse them first; this is the second refusal, and
  it exists because a store whose immutability depends on one check has one
  check to get wrong.
- **The backend may not emit an event.** Structural, per §8.5: nothing in this
  file includes `event_emit.h`, and the test asserts the ELF contributes no
  descriptors from it.

- [ ] Step 1: failing tests — a leaf renders its events in order; a page boundary
      splits between records and never inside one; an unknown activity is
      `NOT_FOUND`; every write verb is refused; a read past the end returns zero
      moved rather than an error.
- [ ] Step 2: the backend.
- [ ] Step 3: `cd sw/userspace && make test && make sanitize && make analyze`.
- [ ] Step 4: commit.

### Task 6: Wired to the machine

**Files:** `sw/userspace/supervisor/src/vfs_host.c`,
`sw/userspace/supervisor/src/main.c`, `sw/userspace/supervisor/src/console_shell.c`

- A second `AstraVfsService` over the events backend, a second client, and
  `EVENTS:` bound with `ASTRA_RIGHT_READ` alone.
- **The Kit routes by the assign's handle.** Two entries today; a
  `ponytail:` note that this is a port handle the day processes exist. This is
  the one place the shell's "one client" assumption has to go, and it has to go
  anyway.
- The drain runs where the supervisor already has a place to do periodic work,
  bounded per pass, so a burst costs several passes and never a stall.
- Failure to start the events service is not fatal: the machine boots without
  `EVENTS:`, which is the layout spec's rule that a binding that cannot be made
  is omitted rather than fatal — and it says so, loudly.

- [ ] Step 1: the wiring.
- [ ] Step 2: extend `emu/qemu/test-events.py`: type a command that fails, read
      the activity id out of the ring, then `cat EVENTS:activity/<id>` in the
      terminal and assert both the acceptance and the refusal appear, in order,
      with their file and line. That is the end-to-end claim of this plan and it
      is one assertion.
- [ ] Step 3: the whole gate on Beast — 30 kernel suites in both configurations,
      userspace test/sanitize/analyze/cross-build, `ext4-test`, the terminal
      gate, the events gate, and the boot budget under 1.00s.
- [ ] Step 4: commit and update the handover.

---

## What this plan deliberately does not do

No `events` command and no filters composed across dimensions — plan 6. No token
bucket and no coalescing, with the triggers written above. No `PROC:`, which is
the third tree and which this plan's backend is the proof of concept for. No
cross-process activity adoption: it is on the wire and waits on the port
transport.
