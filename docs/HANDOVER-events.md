# Astra 68 — Handover: EVENTS: is a filesystem

Date: 2026-08-06. Written to be read cold in a fresh session.

The machine can now be asked what happened, on the machine, through `cat`:

```
WORK:> cat events:activity/00000006
seq 122  info  10000011/16 act 00000006  command accepted, 3 words
                                         (src/console_shell.c:493)
seq 124  warning  10000011/16 act 00000006  command refused, status 6
                                         (src/console_shell.c:183)
```

Plan 5 of six is **done**: the ring is drainable, `readdir` is a cursor, the
catalog is on the machine, the store has its tiers, and `EVENTS:` is a synthetic
tree served through the ordinary VFS backend seam. Plan 6 — the `events`
command — is what remains.

Before this the namespace stopped being a Kit and became how the machine names
files, and statuses got one vocabulary and a verdict a program cannot forge.
Two events, one activity, and **nothing in the shell passed that id to either
of them** — that was true in the ring already; what is new is that the machine
itself can show it.

`docs/HANDOVER-debug-and-namespace.md` is still accurate about the debug
surface. **Everything is on `main`.**

---

## 1. Where things stand

`origin/main` is at `d1fef0c`. **Twenty-eight commits are local and unpushed**,
from `06dd882` (assign-rooted path parsing) to `87ea376` (the machine can be
asked what happened).

Every gate is green on Beast: 30 kernel suites in both configurations,
userspace test/sanitize/analyze/cross-build, `ext4-test`, 29 pytest cases in
`tools/tests`, the terminal gate — now ten lines, four of them `EVENTS:` — the
events gate, and the boot budget at 0.09s of 1.00s.

**Nothing here was believed from the Mac.** The Mac cannot run `make analyze`
at all — `ANALYZER_CC=gcc` is Apple clang, which has no `-fanalyzer`.

## 2. Resume here

**Plan 6: the `events` command** —
`docs/superpowers/plans/` does not have it written yet, and the spec's §7.3 is
the scope: filters composed across level, subsystem, process and time; a live
tail; and §9.1's level change for the current boot. A path is already the
interface for everything a directory can express, so this is search and nothing
else.

Before that, two things this session left on the table, both written into
`2026-08-06-event-store-and-namespace.md` with their triggers:

- **The store is RAM.** The tiers, the budget and the eviction accounting are
  the real piece; what is missing is the write to the state volume's `events/`,
  and therefore the spec's *last M boots*. `EVENTS:boot/-1` does not exist
  rather than existing empty. Most questions worth asking are about a boot that
  has already ended, so this is the next thing after plan 6 rather than a
  someday.
- **No token bucket (§8.4) and no coalescing (§8.3).** The spec asks for a
  measured workload and there was none until the store existed. The eviction
  accounting is in place to supply one.

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
| The ring drained into userspace, gated on DEBUG | `ASTRA_SYSCALL_TRACE_READ` |
| A directory scan that resumes instead of restarting | `AstraVfsBackendOps.readdir`, protocol v2 |
| The catalog on the machine, as an index | `sw/userspace/runtime/src/event_catalog.c` |
| Four tiers, four budgets, eviction accounted | `sw/userspace/events/src/event_store.c` |
| `EVENTS:` as a tree — `ls`, `cat`, no new protocol | `sw/userspace/events/src/event_backend.c` |
| One command's story, read on the machine | `emu/qemu/test-terminal.py` |

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
  Until then the drain skips kernel records — they are in the ring, they are not
  in `EVENTS:`.
- **The store is RAM, so history does not survive a reboot.** `EVENTS:boot/-1`
  is absent rather than empty, which is the honest shape of a promise the
  machine cannot yet keep.
- **The boot ring is its own leaf, `boot/current/earliest`.** Merging it into
  `all` would show every early event twice, because it is a copy.
- **`ls EVENTS:activity` costs a rescan per entry.** Marked `ponytail:` in
  `event_backend.c`; the upgrade is a small ring of distinct activities kept in
  the store. Bounded today by the store's size, which is a few hundred records.
- **`EVENTS:` is bound by the mounter, not by a manifest.** `serves EVENTS:r`
  is written into the layout spec's §3.2 and nothing reads a manifest yet.

## 6. Traps this session found

- **`sw/boot` carries the user image, so it must be rebuilt after
  `sw/userspace`.** A rebuilt supervisor that is not re-ROMmed boots the
  *previous* one, while `test-events.py` decodes the ring against the *new*
  ELF's catalog — so every id resolves to the wrong message and the levels and
  arguments all look shifted by one. That reads as a catalog bug and is a stale
  ROM. **Cost most of an hour.** `cd sw/userspace && make all && cd ../boot &&
  make astra_boot.bin`, every time.
- **m68k aligns a `uint32_t` to two bytes, not four.** A reader that demanded
  four-byte alignment refused the machine's own catalog while every host test
  passed, because x86 happened to give it four. Ask the compiler:
  `_Alignof(T)`. This will bite anything else that validates a buffer it was
  handed.
- **Two services each number their sessions from one.** A router that matched
  an assign to a client by session handed every `EVENTS:` path to the volume,
  which lists the wrong filesystem rather than failing. The handle carries the
  router's slot now.
- **A journal replay lands on top of anything `debugfs` wrote.** An image a
  machine has run has transactions pending; lwext4 replays them at mount and
  the file is on the host and absent on the machine. `e2fsck -fy` first — and
  not in place, because `e2fsck` cannot reopen an `image?offset=` target after
  recovering a journal. `emu/qemu/astra_image.py` lifts the volume out, fixes
  it, and puts it back.
- **`git ls-files | tar` ships only what git tracks.** A new file that has not
  been `git add`ed is missing on Beast, and the failure is a missing rule for a
  source that is plainly there in the editor.
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

# Both gates put this build's catalog on a copy of the volume first, so the
# machine resolves ids the way a built machine actually would.
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

- Persisting the store to the state volume's `events/`, which is what makes
  `EVENTS:boot/-1` mean anything. §2 above.
- The shell language, which the startup manifest's syntax will want.
- The bundle manifest: how an application declares the authority it wants.
- The event system's numbers — tier budgets, token-bucket rates, boot ring size,
  coalescing window — which want a measured workload rather than an opinion. The
  eviction accounting that supplies one is now built.
- Whether the console should keep rendering every event now that `EVENTS:`
  exists. It currently repaints the terminal's own plane, which is the noise in
  every screenshot in this document.
- Whether an unresolved assign that the system knows exists should say so
  (`EVENTS: is held by the events service — try events`) rather than
  `not found`, which is what a typo also gives. Nothing leaks: the standard
  assign names are in the spec.
