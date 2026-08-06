# Event Activity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Plan 4 of six. One request becomes one story. Every event emitted
while the machine is doing a thing carries the same activity id, so `write
hello.txt` is a handful of lines a person can read together rather than a
handful scattered through everything else that happened.

**Architecture:** The activity is **the kernel's, per thread**, not a number
passed with every event. One syscall begins or adopts one; every `LOG_WRITE`
from that thread is stamped automatically. That is one syscall per unit of work
and zero per event, and it is the only arrangement where a program cannot
accidentally emit an event outside the story it is part of.

The storage protocol's request record carries the activity across a process
boundary. `AstraVfsRequest.reserved` becomes `activity` — same offset, same
width, so the wire record does not change shape.

**Tech Stack:** C11. Kernel work, so every gate is Beast.

## Global Constraints

- Design authority: `2026-08-06-event-system-design.md` §4.
- **Activities are flat.** No parent, no spans, no nesting. Nesting brings
  lifetime questions, and `OBSERVABILITY.md`'s rule that a system must not
  report what it cannot substantiate applies to causality too.
- **No program writes correlation code.** If a call site has to remember to
  pass an activity, the ones that matter will forget.
- An id is unique within a boot and zero means "no activity", which is what
  every event carries today.

## Where this stops short, on purpose

**Adoption across a process boundary is recorded but not executed.** The Kit
marshals the caller's activity into every request, and the service core keeps
it — but the storage service runs *in the supervisor's own process, on the
caller's own thread*, because userspace cannot start a process. Adoption is
therefore already true and there is nothing to execute: the thread handling the
request is the thread that made it. The field is on the wire now because that is
the expensive half to add later; the `astra_activity_adopt` call belongs to the
port transport, and arrives with it.

**One cached activity per process, not per thread, in the runtime.** The kernel
holds the truth per thread. The runtime caches the last value it set so the Kit
can marshal it without a syscall per request, and that cache is process-wide.
Correct today — the shell is one thread and the protocol is synchronous — and
wrong the moment a process has two threads doing different things, which is
what the `ponytail:` comment on it says.

---

### Task 1: The kernel owns the activity

**Files:** `sw/include/astra/syscall.h`, `sw/kernel/thread.h`, `sw/kernel/thread.c`,
`sw/kernel/process.c`, `sw/kernel/tests/test_process.c`

- `ASTRA_SYSCALL_ACTIVITY` (45). `data[1] == 0` begins a fresh one; anything
  else adopts that value. Both return the thread's current activity.
- `KernelThread.activity`, zero at creation and at reuse.
- `LOG_WRITE` stamps `record.activity` from the calling thread.

Ids come from a kernel counter so they are unique across processes without
anyone coordinating. Zero is never issued: it is what "no activity" means, and
an allocator that could return it would make the two indistinguishable.

- [x] Step 1: failing tests — a fresh id is non-zero and increases; adopting
      sets it; an event carries it; a second thread has its own; a retired
      thread's slot does not inherit the last one's.
- [x] Step 2: the field, the counter, the syscall, the stamp.
- [x] Step 3: `cd sw/kernel && make test` on Beast.
- [x] Step 4: commit.

### Task 2: The runtime wrappers

**Files:** `sw/userspace/runtime/include/astra/runtime.h`,
`sw/userspace/runtime/src/log.c`, `sw/userspace/runtime/tests/test_runtime.c`

`astra_activity_begin()`, `astra_activity_adopt(id)`, `astra_activity_current()`.

- [x] Step 1: failing tests against the syscall mock.
- [x] Step 2: the wrappers and the cache.
- [x] Step 3: `cd sw/userspace && make test`.
- [x] Step 4: commit.

### Task 3: The activity crosses the protocol

**Files:** `sw/include/astra/vfs_service.h`, `sw/userspace/vfs/src/vfs_client.c`,
`sw/userspace/vfs/src/vfs_service_core.c`, and their tests.

`reserved` becomes `activity`; the client fills it from
`astra_activity_current()`; the core stops refusing a non-zero value there and
keeps it as the session's current activity.

- [x] Step 1: failing tests — a request carries the caller's activity; a
      request with none is still valid.
- [x] Step 2: the field and the client.
- [x] Step 3: `cd sw/userspace && make test && make sanitize`.
- [x] Step 4: commit.

### Task 4: The shell tells the story

**Files:** `sw/userspace/supervisor/src/console_shell.c`

A command line begins an activity. The shell emits one `notice` naming the
command and one `warning` for a refusal, both carrying it — which is what makes
a refused command a story rather than a line on a screen that nothing recorded.

- [x] Step 1: the call sites.
- [x] Step 2: extend `emu/qemu/test-events.py`: type a command that fails,
      then assert two events share one non-zero activity.
- [x] Step 3: the whole gate on Beast.
- [x] Step 4: commit and update the handover.

---

## What this plan deliberately does not do

No `events --activity` command: that is plan 6, and until then the decoder's
`act %08x` column is how a story is read. No service, no retention, no rate
limiting — plan 5.
