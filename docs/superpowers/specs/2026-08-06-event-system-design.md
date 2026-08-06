# Astra event and logging system

Date: 2026-08-06
Status: design, approved in conversation. §1.1's record, §6.1's authority
reversal and §6's one-stream rule are built; the macro, the catalog, activity,
the service and the `events` command are not.

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
most four arguments and at most **24** bytes of them; a call site wanting more
is a call site that wants two events.

24 rather than 32 because the second ring slot spends eight bytes on its own
commit sequence and its discriminator, without which a slot stops being
self-describing and a reader cannot answer for one in isolation. Four `u32`
arguments or three `u64` ones fit; four `u64`s is the one combination that does
not, and the macro refuses it at compile time.

The inline string is the escape hatch and it is deliberately awkward, because
it is the one argument type that costs what text logging costs.

**Log the object, not the name.** Where an object exists — a file, a mount, a
process — the event carries its identity and a reader holding the right
authority resolves it back to a name. This is redaction by not having the data,
which is the only kind that cannot leak, and it is the same rule as "identity
is the object, never the string" in the layout spec.

An inline string is therefore for the case where there is no object: a volume
label read off a disk that would not mount, a name that failed to resolve. An
event copied off this machine is less legible as a result, and that is the
correct trade rather than a regrettable one.

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

**The kernel's own events use it too.** The existing `KernelTraceEvent` enum —
`SYSCALL_ENTRY`, `IRQ_DELIVER`, `PMMU_FAULT` and the rest — becomes descriptors
like everything else, so there is one event model, one catalog format and one
reader for the whole machine. Two kinds of event would be the first exception
to "one way to do each thing", and the first exception is the one that teaches
everyone the rule is negotiable.

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

Universal emission needs a bound; §8.4 is the bound. When the ring overflows
before the service drains it, the service records *lost N events* rather than
dropping silently — the same rule the input FIFO already follows for overflow.

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

## 8. Retention: what rolls off, and when

Runaway logs are the failure that bites every system, and a single bounded ring
with drop-oldest is not enough to prevent it. It solves the disk filling and
leaves the worse problem: one subsystem in a retry loop evicting the failure
you were looking for.

### 8.1 Level decides the store; each store has its own budget

Not time. Time-based expiry with no size bound fills disks; a size bound with
no structure loses signal to noise.

| Tier | Holds | Sizing |
|---|---|---|
| `presented` | events the person was shown — the user-visible bit | tiny volume, longest life; this is the notification history |
| `record` | notice, warning, error | the default working history |
| `detail` | info | largest volume, shortest life |
| — | debug | **never persists**: live subscription only, dropped at drain |

Independent budgets are the point. A subsystem screaming at `info` can evict
only `info`. It cannot reach the errors, and it cannot reach what the person
was told.

### 8.2 The boot ring

The earliest events are the most valuable and a first-in-first-out store
evicts them first. A small dedicated ring keeps the first N events of the last
M boots, always, outside the tier budgets.

Boot is when a debugger cannot be attached and when the most expensive failures
happen. This is the cheapest possible fix for "the log rolled before I got
there".

### 8.3 Identical events coalesce

Same message id, same arguments, within a window: one stored record with a
repeat count and a last-seen tick. Syslog's "last message repeated 500 times",
except structured — the count and the span both survive, rather than both being
lost.

This is the largest single source of log bloat and it is nearly free here,
because an event is already an id and typed arguments rather than a formatted
line.

Coalescing happens at drain, so a **stored** record is the §1.1 ring record
plus a repeat count and a last-seen tick. The ring record itself never changes
shape; the kernel does no matching.

### 8.4 Emission is rate limited per subsystem

A token bucket — a sustained rate and a burst — per subsystem. A loop that
emits a million events spends its own budget, and the excess is counted as loss
**attributed to that subsystem**, so a reader sees `storage lost 4,201 events`
rather than a history that is mysteriously short.

Throttling at the source is what keeps the store's rules simple: there is one
ring per tier, and no subsystem has a reserved share of it. The consequence,
stated rather than hidden: within a tier, a subsystem that sits just under its
rate for hours still evicts older events belonging to a quieter one.

**Eviction is recorded** — what was dropped, and from which subsystem — so if
guaranteed per-subsystem floors turn out to be needed, that case gets made from
evidence rather than from taste. Adding floors later changes the store layout,
which is why the evidence is collected now.

### 8.5 Logging may never cause the failure

- **Emitting is a copy into the kernel ring.** Never a disk write, never a
  block. A program's syscall does not wait for storage, ever.
- **The service drains off the critical path.** A slow or full disk becomes
  loss markers, not a stalled machine.
- **The events service may not emit an event caused by handling an event.**
  That feedback loop is how a logging subsystem takes a machine down, and the
  rule against it has to be structural rather than careful.
- **A broken retention configuration means defaults**, loudly — the layout
  spec's parse-fully-or-not-at-all rule already requires this.

### 8.6 Where the stores live

`EVENTS:` holds the tiers and the boot ring. The kernel trace ring is the
transport, not the store: history is bounded twice, by the ring for the live
window and by the budgets for the durable one.

## 9. Configuration, and turning it off

One file, `events.conf`, in `DEFAULTS:` with overrides in `CONFIG:`. A level per
subsystem, a global default, and **one number**: the total events budget. The
three tiers and the boot ring are fixed fractions of it, so a person sets one
value and cannot produce an incoherent split — a tiny record tier beside a huge
detail tier looks reasonable right up to the moment you need yesterday's error.

The starting fractions and the token-bucket rates are provisional. §8.4's
eviction accounting is what corrects them, on a real workload rather than by
invention. That is the whole surface. No filters, no pipelines, no routing rules, no plugin
directory — an event system with a configuration language is a program nobody
can predict, and §"Why this shape" of the layout spec says why that is not on
offer.

### 9.1 Raising a level while something is wrong

Levels apply at boot, like everything else in `CONFIG:`. The one exception is
diagnostic rather than configurational: `events` can raise a subsystem's level
**for the current boot**, because the fault you cannot reproduce is the one you
cannot afford to reboot away from.

It says that it is temporary, and `events` shows any level that differs from
the file. This is deliberately a second way to set one thing, which the service
rule refuses — the difference is that a service set changes rarely and never
mid-investigation, while a log level is exactly the thing you need to change
while the evidence is still on the machine.

### 9.2 What "off" actually costs

Three different things get called off, and they are not the same:

| What | Cost of an event at a call site | What is lost |
|---|---|---|
| Subsystem level raised | one load, compare and branch | events below the level, from that subsystem |
| Global level `off` | the same branch, everywhere | the machine's account of itself |
| Not compiled in | nothing | requires rebuilding, so not available to a person with a shipped system |

A person who turns events off gets the second row: **one branch per call site**,
no syscall, no ring traffic, no service, no disk. Zero is only reachable by
rebuilding, and this design does not pretend otherwise.

Not starting the events *service* is a separate saving — a process, its memory
and its disk writes — and it is a separate line in the manifest. Doing only
that leaves call sites still paying for a syscall into a ring nobody drains,
which is the worst of both and is worth saying out loud.

### 9.3 A disabled log must never look like an empty one

If capture is off, the machine says so — at boot, and in the `events` command's
output, in place of the empty list it would otherwise print. Silence and
nothing-happened are different facts and a machine that renders them
identically is lying about the more important one.

Hiding the switch would be worse than the switch. A person is allowed to turn
this off; they are not allowed to be confused later about whether they did.

## 10. What this changes in existing code

| Piece | Change |
|---|---|
| `ASTRA_SYSCALL_LOG_WRITE` | becomes the event append: message id and typed arguments, into the trace ring |
| Its authority | ungated to emit; `ASTRA_RIGHT_DEBUG` moves to reading and to the console sink |
| `sw/kernel/trace.c` | gains the user record type and per-process emission accounting; its own event enum becomes descriptors |
| `sw/kernel/monitor.c` | the `trace` command renders through the catalog |
| Message headers | gain the activity field; the VFS Kit sets it |
| `sw/userspace/runtime` | `ASTRA_EVENT`, the descriptor section, the per-subsystem enable words |
| Build | a step extracting descriptors into a catalog, for the kernel and per bundle |
| `astra_assert_failed` | emits an event instead of formatting a line |

## 11. Open questions

- The provisional numbers behind the single budget knob: the tier fractions,
  the token-bucket rates, the boot ring's size and the coalescing window. These
  need a real workload rather than an opinion, and §8.4's eviction accounting
  is what will supply one.
- Whether an activity should ever be attributed to a person's action in the
  interface, once there is an interface to attribute it to.
