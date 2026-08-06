# The `events` Command Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Plan 6 of six, and the last of the event system. `EVENTS:` already
answers *show me* — `cat` does that. This is the half a path cannot express:
the last screen rather than the whole file, two dimensions at once, and a live
tail.

**Architecture:** The command is a **path builder and a pager**, not a client.
It composes a path out of its flags, reads it through the ordinary VFS Kit, and
prints. Everything it can ask for is something `cat` could have asked for, which
is the property that keeps `events` from becoming a second door onto the store.

Two dimensions at once is the one thing today's tree cannot name, so the tree
grows a level under each subsystem — `EVENTS:subsystem/storage/warning` — and
the command builds that path. That is a filter the backend already holds both
halves of; it is four lines of parser and no new mechanism.

**Tech Stack:** C11, userspace. Host tests for the backend and the parser; the
QEMU terminal gate for the command itself.

## Global Constraints

- Design authority: `2026-08-06-event-system-design.md` §7.3 and §9.1.
- **Everything `events` shows, a path could have shown.** If a flag cannot be
  expressed as a path, either the tree grows the path or the flag does not
  exist. A command that can see what the filesystem cannot is the bespoke query
  service this design refused.
- **Bounded memory.** The last-N-lines pager holds N lines, not the file.
  Nothing renders an unbounded result on a 32 MiB machine.
- **A live tail must be escapable.** A terminal with no way back to the prompt
  is worse than no live tail at all.

## Where this plan stops short, on purpose

**No `--boot -1`.** The store is RAM (plan 5, task 4), so there is exactly one
boot to show. The command says that in one line rather than reporting an empty
result, because silence and nothing-happened are different facts. **Trigger:**
the store persisting.

**No time range.** There is no wall clock (§5): every event carries monotonic
ticks and the boot's identity, and a range in ticks is a number nobody can type
usefully. **Trigger:** the clock offset event arriving.

**No `--level-set` (§9.1).** Raising a subsystem's level for the current boot is
a *control* operation on the events service, and control is a capability rather
than a write — `OBSERVABILITY.md`'s rule. The per-subsystem level words live in
each process's own runtime, so setting another process's level needs the port
transport that does not exist yet. Doing it today would only ever set the
shell's own levels, which is a lie the moment there is a second process.
**Trigger:** the port transport.

**No `--process`.** Cheap to add — it is the same generated directory the
activity one is — and not yet worth a second quadratic listing while there is
one process. **Trigger:** a second process.

---

### Task 1: A subsystem has levels

**Files:** `sw/userspace/events/src/event_backend.c`,
`sw/userspace/events/tests/test_event_backend.c`

`EVENTS:subsystem/<name>` gains the four level leaves it already has under
`boot/current`, so `subsystem/storage/warning` is a path and
`subsystem/storage/all` is every level of it. The subsystem itself becomes a
directory: a node that were both a file and a directory would have to answer
`ls` and `cat` with two different kinds, and the protocol refuses a read on a
directory.

The node already carries both filters; this is the path parser and the listing.

- [x] Step 1: failing tests — `subsystem/storage/warning` holds the storage
      warnings and nothing else; `subsystem/storage/all` is every storage
      event; the directory lists its four leaves; an unknown level under a
      known subsystem is `NOT_FOUND`; a name that merely starts like a known
      one is not it.
- [x] Step 2: the parser and the listing.
- [x] Step 3: `cd sw/userspace && make test && make sanitize && make analyze`.
- [x] Step 4: commit.

### Task 2: The command

**Files:** `sw/userspace/supervisor/src/console_shell.c`

```
events                          the last screen, notice and above
events --all                    every level, including info
events --level warning          warning and above
events --subsystem storage      one subsystem
events --activity 1a2b          one request, across every process
events --follow                 live, until a key is pressed
```

- A flag combination becomes one path. `--subsystem storage --level warning` is
  `EVENTS:subsystem/storage/warning`; `--activity` is its own leaf and refuses
  to be combined, because an activity is already a slice through every
  dimension.
- **The last screen, not the whole file.** A ring of the terminal's height in
  line offsets, filled by reading forward; then the tail is re-read and printed.
  Bounded by the screen rather than by the store.
- **`--follow` ends on a keypress.** It reads from where the file stopped
  growing, prints what is new, and polls the input device between passes — the
  same input handle the shell already holds.
- **What is not kept is said plainly.** `--boot`, `--since` and `--level-set`
  are recognised and answered with the reason they do not work yet, rather than
  parsed as a path and reported as `not found` — which is what a typo gives, and
  the two must not look the same.

It is a shell builtin, like every other command here, and moves to `COMMANDS:`
with the loader. The shell is where the parser and the pager already are.

- [ ] Step 1: the command.
- [ ] Step 2: `cd sw/userspace && make test && make sanitize && make analyze`
      and the cross-build.
- [ ] Step 3: the terminal gate — `events` shows the boot's own notices, and
      `events --subsystem shell` shows the command that was just typed and
      nothing from the volume.
- [ ] Step 4: the whole gate on Beast, then commit and update the handover.

---

## What this plan deliberately does not do

No pipe, no output redirection, no regex. `events` is one screen of one filter;
composing beyond that is what a shell language is for, and that is its own spec.
