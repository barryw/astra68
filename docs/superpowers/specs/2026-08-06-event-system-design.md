# Astra event and logging system

Date: 2026-08-06
Status: design, approved in conversation; not implemented

Depends on `2026-08-06-filesystem-layout-design.md`, which fixes where the
store lives and settles that program lines and kernel events share one ordered
stream.

## Why

A program on this machine can currently say two things about itself: a
monotonic integer through the progress counter, and one halfword of exit
status. The console channel added in `2a995b8` fixed the immediate problem and
is not an event system — it is unstructured, unqueryable, and gated on a
capability, which means the machine's account of what happened has holes
exactly where something went wrong.

Three constraints shape every decision below:

- **The machine is slow.** ~30 MHz equivalent. Formatting a string at the point
  of logging is real work, and a system nobody can afford to use is not a
  logging system.
- **The store is bounded.** Bytes per event decide how much history survives.
- **A request crosses four processes.** `write hello.txt` is shell → Kit →
  service → backend → block. Every difficult failure this project has had
  spanned at least three of those, and nothing correlated them.

---

## 1. The record

### 1.1 What an occurrence carries

| Field | Bytes | Why it varies per event |
|---|---|---|
| sequence | 4 | total order, from the shared ring |
| monotonic ticks | 8 | when, relative to this boot |
| process | 4 | who, generation-tagged per `OBSERVABILITY.md` |
| thread | 2 | which thread of it |
| activity | 4 | what caused it — see §4 |
| message id | 4 | which event this is — see §3 |
| level and flags | 2 | severity, user-visible bit, argument count |
| arguments | ≤ 32 | the values that differ between occurrences |

Fixed part is 28 bytes; a record is 32 bytes with no arguments and 64 with a
full set. The ring is the kernel's existing trace ring, whose records are
already 32 bytes.

### 1.2 Static context is free

Subsystem, source file, source line, the format string, and the argument types
are properties of the *message*, not of the occurrence. They live in the
catalog, indexed by message id, and cost **zero bytes per event**.

The consequence worth stating plainly: every event knows which file and line
emitted it, and nobody types that anywhere.

### 1.3 Rare facts are events, not fields

Anything that changes rarely is recorded once, as its own event, and the reader
joins it to everything after:

- *process 0x10000011 is `APPS:Editor`, build 0x18EBE2E1* — emitted at launch
- *at monotonic T, the wall clock was X* — emitted when a clock arrives (§5)
- *ring lost N events* — emitted when the ring overflows (§6.2)

This is what keeps records at 32 bytes while the reader still shows an absolute
time, a program name, and a build. A field repeated in every record to describe
something that changed twice is the usual way logs get fat.

### 1.4 Argument types

A bounded set: `u32`, `u64`, `s32`, `status`, `handle`, and one bounded inline
string for the case where a name genuinely varies — a path, a volume label. At
most four arguments and at most 32 bytes of them; a call site wanting more is a
call site that wants two events.

The inline string is the escape hatch and it is deliberately awkward, because
it is the one argument type that costs what text logging costs.

---

## 2. Emitting

```c
ASTRA_EVENT(STORAGE, ASTRA_EVENT_WARNING,
            "write refused, status %u after %u sectors", status, sectors);
```

The macro does two things. It emits a **descriptor** — subsystem, level, file,
line, format, argument types — into a non-loaded ELF section, and it compiles a
call carrying the descriptor's address as the message id plus the arguments.

Nothing is hashed, nothing is registered at startup, and the id is stable for
the life of a build.

A disabled level costs **one branch** against a per-subsystem enable word. That
is the price of leaving `debug` events compiled into shipping code, and it is
low enough that they should be.

`sw/kernel/user_copy.c` already uses this exact pattern —
`_kernel_user_copy_sites_start/end` is a table of descriptors in a dedicated
section, consumed by the fault handler. The mechanism is proven here.

---

## 3. The catalog

A build step extracts the descriptor section into a catalog: message id →
subsystem, level, file, line, format, argument types.

- For an application, the catalog ships **inside the bundle**, so an
  application's events are readable wherever it goes.
- For the kernel and the system, it ships on `SYS:`.

A reader resolves ids using the catalog for the image that process was running,
which it learns from the process-start event in §1.3.

Two consequences:

- **Old events stay readable** as long as the catalog that made them is around.
  A bundle carrying its own catalog is a bundle whose logs survive its own
  replacement, if the store outlives it.
- **Messages are translatable and greppable without parsing text**, because the
  format string is data in a table rather than bytes in a stream.

---

## 4. Activity: one story across four processes

Every request message carries the activity that caused it. The client Kit sets
it from the calling thread's current activity; a service adopts it for the
duration of handling; every event either emits inherits it.

```
activity 0x1a2b  shell     command accepted: write hello.txt
activity 0x1a2b  vfs Kit   open, flags CREATE|TRUNCATE
activity 0x1a2b  vfs svc   session 3, handle 1 opened
activity 0x1a2b  ext4      transaction started
activity 0x1a2b  block     write refused, status 5
```

No program writes correlation code. The protocol records already exist; this is
a field in the message header the Kit fills in, and a per-thread current
activity it maintains.

An activity is started by whatever begins a unit of work: a keystroke reaching
the shell, a launch, a boot step. Activities are flat — no parent, no spans.
That is a deliberate stopping point: nesting brings lifetime questions and
`OBSERVABILITY.md`'s rule that a system should not report what it cannot
substantiate applies to causality too.

---

## 5. Time

Every event carries monotonic ticks and the boot's identity. Ordering is by
sequence number and never by a timestamp, so it is correct regardless of what
any clock does.

The wall clock arrives as an event — *at monotonic T, the wall clock was X* —
and a reader converts every event in that boot exactly. Before it arrives,
events show as `boot 7, +4.2s`, which is true, rather than as 1 January 1970,
which is not.

### 5.1 Where a wall clock could come from

The machine has no RTC. Three sources, none built:

1. **The Linux side, at boot.** On the Arty the PS runs Linux hosting the
   emulator, and Linux has NTP. Handing the time across the AstraHost transport
   during startup is the cheapest real clock this machine can have, and it
   arrives exactly as the offset event above. On Beast the same path works from
   the host.
2. **NVRAM, as a floor.** Storing the last known time at shutdown gives a
   plausible starting point rather than an epoch, which is what machines did
   before networks. It is a floor, never a truth: the reader must show it as
   approximate.
3. **A real RTC**, if hardware ever grows one. Nothing above changes; it
   becomes a fourth source of the same offset event.

The design does not care which arrives, or whether two do. A later, better
source emits another offset event and the reader prefers it.

---

## 6. Who may emit, and what happens when the ring fills

### 6.1 Emitting is universal; reading is authority

**Every process may emit.** If the machine's account of what happened depended
on a capability, the account would have holes precisely where something went
wrong.

**`ASTRA_RIGHT_DEBUG` gates reading** other processes' events and attaching to
the console sink. Logs are where secrets leak, so observation is the privileged
half.

This reverses the console channel as built in `2a995b8`, where the *write* was
gated. That was right for a debug console and is wrong for the system of
record.

### 6.2 Emission is accounted, and loss is recorded

Universal emission needs a bound. Each process has an emission budget; events
beyond it are dropped and counted. When the ring overflows before the service
drains it, the service records *lost N events* rather than dropping silently —
the same rule the input FIFO already follows for overflow.

A log that quietly loses records is worse than one that admits it, because
everything read from it afterwards is an assumption.

---

## 7. Reading

### 7.1 A service, queried from the terminal first

The events service owns the store; nothing else holds `EVENTS:`. It answers
bounded queries — by activity, level, subsystem, process, time range — and a
live subscription.

**The terminal client is the first client and stays first-class.** A
Console-style window is a second client of the same protocol, not a replacement
for it, and nothing about the store or the protocol assumes a GUI exists.

```
events                             the last screen of notice and above
events --level debug --subsystem storage
events --activity 1a2b             one request across every process
events --follow                    live, as they happen
events --boot -1                   the boot before this one
```

`--activity` is the one that pays for the whole design: it is the command that
would have shown, in one screen, the input-read refusal that took a session to
find.

### 7.2 Bounded answers

A query returns a bounded page and a cursor, like every other enumeration in
this system. Nothing renders an unbounded result into memory on a 32 MiB
machine.

---

## 8. Storage

`EVENTS:` holds a bounded on-disk ring with a size budget, dropping oldest.
It never grows without limit, because a machine that will not boot after
logging too much about not booting is `/var/log` in a nicer hat.

The kernel trace ring is the transport, not the store: the service drains it
into `EVENTS:` and the disk ring is what survives a reboot. History is
therefore bounded twice — by the ring for the live window, by the budget for
the durable one.

---

## 9. What this changes in existing code

| Piece | Change |
|---|---|
| `ASTRA_SYSCALL_LOG_WRITE` | becomes the event append: message id and typed arguments, into the trace ring |
| Its authority | ungated to emit; `ASTRA_RIGHT_DEBUG` moves to reading and to the console sink |
| `sw/kernel/trace.c` | gains the user record type and per-process emission accounting |
| Message headers | gain the activity field; the VFS Kit sets it |
| `sw/userspace/runtime` | `ASTRA_EVENT`, the descriptor section, the per-subsystem enable words |
| Build | a step extracting descriptors into a catalog, for the kernel and per bundle |
| `astra_assert_failed` | emits an event instead of formatting a line |

## 10. Open questions

- The emission budget's actual numbers, which need measurement rather than
  invention.
- Whether the kernel's existing trace events get message ids too, or stay a
  separate enum the reader special-cases.
- How a person adjusts per-subsystem levels at runtime, and whether that is
  configuration in `CONFIG:` or a command against the service, or both.
- Redaction: an inline string argument can carry anything a program has, and
  the machine has no notion yet of a value that should not be recorded.
