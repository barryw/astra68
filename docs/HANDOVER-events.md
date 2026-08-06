# Astra 68 — Handover: the event system, statuses, and the namespace wired on

Date: 2026-08-06. Written to be read cold in a fresh session.

Three things landed. The namespace stopped being a Kit and became how the
machine names files. Statuses got one vocabulary and a verdict a program cannot
forge. And the event system went from a design to four of its six plans, ending
with a line a person can read:

```
seq 206  info     10000011/16 act 00000001  command accepted, 3 words
                                            (src/console_shell.c:467)
seq 208  warning  10000011/16 act 00000001  command refused, status 6
                                            (src/console_shell.c:171)
```

Two events, one activity, and **nothing in the shell passed that id to either
of them.**

`docs/HANDOVER-debug-and-namespace.md` is the previous resume point and is
still accurate about the debug surface. **Everything is on `main`.**

---

## 1. Where things stand

`origin/main` is at `d1fef0c`. **Twenty commits are local and unpushed**, from
`06dd882` (assign-rooted path parsing) to `a3e3798` (the catalog is the
section).

Every gate is green on Beast: 30 kernel suites in both configurations,
userspace test/sanitize/analyze/cross-build, `ext4-test`, 29 pytest cases in
`tools/tests`, the terminal gate at six lines, the new events gate, and the
boot budget at 0.09s of 1.00s.

**Nothing here was believed from the Mac.** The Mac cannot run `make analyze`
at all — `ANALYZER_CC=gcc` is Apple clang, which has no `-fanalyzer`.

## 2. Resume here

**Execute plan 5, `docs/superpowers/plans/2026-08-06-event-store-and-namespace.md`,
starting at its task 1.**

The spec rewrite this section used to ask for is **done**: the events spec's §7
is now `EVENTS:` as a synthetic tree, with §8.6 and §10 following it, and the
layout spec's §2.2, §2.3, §3.2, §3.5 and §6 moved `EVENTS:` out of the writable
assigns and into the synthetic ones. Two clauses in the startup manifest are new
and general rather than events-specific: `STORE:` for a service's own private
state, and `serves NAME:r` for a synthetic tree a service publishes.

The decision, and why it is not what the spec used to say:

`2026-08-06-event-system-design.md` §7.1 has the events service answering
bounded queries through a bespoke `events` command, and says *"nothing else
holds `EVENTS:`"*. That was questioned and does not survive contact with
`docs/OBSERVABILITY.md`, which already established the pattern:

> *"How much history and **which additional trees live beside `PROC:`** remain
> open. The constraint that matters now is that **no special case is needed to
> add them**."*

and which already imposed the handler-contract requirement that makes it work —
nodes generated at read time, bounded cookie-based enumeration, no assumption
of a stable on-disk size. `EVENTS:` is the second tree, not a new mechanism.

```
EVENTS:
  boot/
    current/  all  notice  warning  error
    -1/ -2/                        the boot ring
  activity/
    0000001a                       one request across every process
  subsystem/
    storage/  shell/  vfs/
```

Read-only by the rights on the assign, which is the same mechanism that already
makes `SYS:` unwritable — so nothing can `rm` an event. `--follow` becomes
re-reading a file that grew.

**The reason this is less work, not more:** `AstraVfsBackendOps` already exists
and is already the seam. `vfs_ext4_backend.c` is one implementation and the
service core is backend-agnostic and host-tested. An events backend is a few
hundred lines behind a tested interface — no new protocol, no new client, and
`ls`/`cat` work unchanged. The `events` command survives as *search*, not as
the only door.

Two costs, now written into the plan rather than left to discover:

- **`readdir` must become a cursor.** It is index-addressed and the ext4
  backend reopens the directory per entry, so it is already quadratic.
  `ls EVENTS:activity` over thousands of entries would be brutal at 30 MHz.
  This is the same debt the layout spec's §1.7 named for union assigns, so one
  piece of work unlocks two things — but it is on the critical path now.
- **The catalog has to be on the machine.** Rendering text at read time means
  resolving format strings there. The file is the `.astra_events` section's
  bytes **verbatim** — `objcopy -O binary --only-section=.astra_events` already
  produces it — and lookup is `(id - base) / 128`. An index, not a parse. Do
  not put a parser on this machine for this.

Plan 5 is six tasks: the ring drain syscall, `readdir` as a cursor, the catalog
on the machine, the store and its tiers, the events backend, and the wiring with
an end-to-end gate. The token bucket (§8.4) and coalescing (§8.3) are
deliberately deferred inside it, each with a written trigger — the spec asks for
a measured workload and there is none until the store exists. Plan 6 is the
`events` command.

## 3. What the machine gained

| Capability | Where |
|---|---|
| Paths are `ASSIGN:rest`; there is no root | `sw/userspace/vfs/src/vfs_path.c`, `vfs_assign.c` |
| `..` cannot climb out of an assign | `astra_path_normalise`, tested both ways |
| Rights checked before a disk is asked | `astra_assign_resolve` |
| One status vocabulary, 0/1–31/32+/verdict | `sw/include/astra/status.h` |
| A crashed process cannot report success | `retire_current`, `ASTRA_STATUS_FAULTED` |
| Every process may emit an event; reading is the right | `ASTRA_SYSCALL_LOG_WRITE`, ungated |
| Typed events with zero-cost static context | `ASTRA_EVENT0..4`, `.astra_events` |
| One activity across a whole command | `ASTRA_SYSCALL_ACTIVITY` |
| A readable stream, off-machine | `tools/trace_decode.py` |
| An end-to-end gate for all of it | `emu/qemu/test-events.py` |

## 4. The three design decisions worth not relitigating

**A status names *which* failure, never how bad one is.** Severity lives on the
event record, where it is ordered and filterable, and it lives there *instead
of* in the status. A status ordered by severity freezes its own numbering the
first time somebody writes `status > N`.
`docs/superpowers/specs/2026-08-06-status-and-exit-design.md`.

**Block-level volume spanning is refused permanently.** Two media under one
filesystem is one failure domain, so pulling a card would destroy the boot
volume. Union assigns — an ordered list of `(mount, root, rights)` — are
designed and deliberately **not built**, with the trigger written down: when one
logical collection genuinely cannot be split by name. Running out of room is not
that trigger. Layout spec §1.7.

**A volume's label is its name.** No `DH0:`/`DH1:` slot namespace; numbering by
discovery order is `/dev/sda` with a colon. `SYS:` is the boot volume by role.
Layout spec §1.6.

## 5. Where the built thing knowingly falls short of the specs

Each of these is deliberate and written into the plan that made it.

- **`SYS:` is read-only by right, not by mount.** One partition, so `SYS:` binds
  the volume root with `ASTRA_RIGHT_READ` and `WORK:` binds `work/` with read
  and write. A `WORK:` holder can still reach the same bytes. A second volume
  fixes it.
- **`vfs_ext4_backend.c` still prefixes lwext4's mount point** and should.
  `"/vol/"` is that backend's own namespace, not the machine's.
- **The event argument payload is 24 bytes, not the spec's 32.** Eight bytes of
  the second ring slot pay for its commit sequence and discriminator, without
  which a slot is not self-describing. Four `u32`s or three `u64`s fit; the
  macro refuses a `u64` at compile time. The spec was amended.
- **Activity adoption across a process boundary is on the wire but not
  executed.** The storage service runs in the supervisor's own process on the
  caller's own thread, so adoption is already true. `astra_activity_adopt`
  belongs to the port transport and arrives with it.
- **One cached activity per process, not per thread, in the runtime.** The
  kernel holds the truth per thread. Marked `ponytail:` with the upgrade path.
- **The kernel's own `KernelTraceEvent` enum is not descriptors.** Converting
  ~50 call sites in the most-tested code buys readability the decoder already
  delivers by reading the enum out of `trace.h`. Optional, per subsystem, later.

## 6. Traps this session found

- **`git ls-files | tar` ships whatever directory you are standing in.** The
  Bash tool's cwd persists between calls. Running it from `sw/userspace` or
  `sw/userspace/supervisor` scatters a subtree into Beast's repo root —
  `~/astra68/src`, `~/astra68/include`, and a stray `~/astra68/Makefile`. **Cost
  two debugging rounds in one session.** Always `cd` to the repo root in the
  same command.
- **tar preserves mtimes, so a shipped source can look older than the object
  built from its previous version.** The Mac and Beast do not agree about the
  time and make believes timestamps: a source that definitely changed produces
  no rebuild and no error. The symptom is a feature present in the source on
  Beast and absent from the binary — here, a shell's event descriptors simply
  missing from the catalog. `find sw -name '*.[ch]' -exec touch {} +` after
  extracting.
- **QEMU's HMP needs the dump filename quoted.**
  `pmemsave 0x020c4000 65536 /tmp/ring.bin` fails with `invalid char 't' in
  expression`; `pmemsave 0x020c4000 65536 "/tmp/ring.bin"` works.
- **A host pointer does not survive the 32-bit syscall ABI.** A host test whose
  mock dereferences what it was handed reads a truncated pointer and segfaults.
  Test pure functions directly instead — `astra_event_pack` exists for that.
- **Mach-O cannot spell `__attribute__((section(".astra_events")))`.** Guarded
  on `__ELF__`; section placement is a link-time property that `readelf` checks.
- **The thread record has a deliberate size assertion.** It went 180 → 184 for
  the activity. Growing it is a decision, not an accident.
- **Reap the emulator, do not just signal it.** A lingering QEMU competes with
  the next gate, and a boot deadline missed for that reason looks exactly like a
  machine that will not boot.

## 7. Reproducing the gates

Everything below is Beast.

```sh
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1
cd sw/userspace && make test && make sanitize && make analyze && make all
cd sw/userspace/storage && make ext4-test
cd sw/boot && make astra_boot.bin

python3 emu/qemu/test-events.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img
python3 emu/qemu/time-boot.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --runs 5 --budget 1.0
```

On the Mac, the Python halves:

```sh
python3 -m pytest tools/tests/
python3 tools/event_catalog.py \
    sw/userspace/supervisor/build/m68k/astra_supervisor.elf
```

Reading a ring by hand:

```sh
# in the QEMU monitor, with the quotes
pmemsave 0x020c4000 65536 "/tmp/ring.bin"
python3 tools/trace_decode.py /tmp/ring.bin \
    --elf sw/userspace/supervisor/build/m68k/astra_supervisor.elf --user-only
```

## 8. Serialization, settled

Three consumers, three forms, and **no JSON anywhere near the machine**:

| Consumer | Form |
|---|---|
| A person | columns, from `event_catalog.py` and `trace_decode.py` |
| Host tools | the ELF section, read in process — there is no catalog file |
| The machine | the section's bytes verbatim; lookup is `(id - base) / 128` |

The general rule that produced this, worth keeping: **when parsing looks
expensive, the first move is not to parse — not to parse somewhere else.**

The related question of offloading work to the ARM was raised and answered:
**offload devices, not computation.** Astra already offloads devices correctly
(block storage over AstraHost, and the wall clock in the events spec §5.1).
A 68882-style FPU passes that test because it is real 030-era hardware. A string
or JSON accelerator fails it — nothing like it existed, and software written
against one only runs on this host. Remember that the 68030 *is* QEMU on those
same ARM cores, so "offload to the ARM" is an escape hatch out of emulation
rather than a coprocessor, and every good decision in this design came from
taking 30 MHz seriously. If string work ever measures hot, the legitimate
accelerator is a blitter — `Astraea` already exists in this tree, with an Arty
successor in `GRAPHICS_ARCHITECTURE.md`. Nothing has measured it hot yet.

## 9. Open decisions, carried forward

- `readdir` as a cursor: needed by `EVENTS:`, by union assigns, and by any
  large directory. Currently quadratic. **Plan 5, task 2.**
- The shell language, which the startup manifest's syntax will want.
- The bundle manifest: how an application declares the authority it wants.
- The event system's numbers — tier budgets, token-bucket rates, boot ring size,
  coalescing window — which want a measured workload rather than an opinion.
- Whether an unresolved assign that the system knows exists should say so
  (`EVENTS: is held by the events service — try events`) rather than
  `not found`, which is what a typo also gives. Nothing leaks: the standard
  assign names are in the spec.
