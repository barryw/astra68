# Astra 68 — Handover: union assigns, and the root a grant was missing

Date: 2026-08-07. Written to be read cold in a fresh session. Read `CLAUDE.md`
first; this is the continuation map for the union-assigns milestone only.
`docs/HANDOVER-launch.md` is the milestone this one continues from and is
still worth reading — the mechanisms in its §3 (a launch, streams, provenance,
the serving wait) are unchanged here.

**`COMMANDS:` is now an ordered two-member union, and a launched child
resolves through it correctly.** Before this milestone a grant could not say
where in its mount a name began, so a launched child's `COMMANDS:` silently
meant the whole volume rather than the directory it was granted — a live
defect nothing had yet tripped over. Both are fixed together, because the
second cannot be proven without the first: `which`, a launched program, holds
`COMMANDS:` as two grants with two roots, seeds them into its own namespace,
and loops them with the same Kit function the shell uses. The terminal gate is
32 of 32.

Branch `union-assigns`, base `b3bc9bb`, HEAD `cd914c9`. Spec:
`docs/superpowers/specs/2026-08-07-union-assigns-design.md`. Plan:
`docs/superpowers/plans/2026-08-07-union-assigns.md` — every finished task
has a **"what the build settled"** block written under it, and those blocks
are the detailed record; this document is the summary worth reading first.

---

## 1. Where the code is

| Task | What | Landed as |
|---|---|---|
| 1 | ABI: both capability records 28→92 bytes, `root[64]` | `19c8504`..`28c59e1` |
| 2 | `ASTRA_LAUNCH_GRANT_MAX` 6→8, one number | `151c5e4`..`6488403` |
| 3 | A member is a repeated name (`astra_assign_join`/`member`) | `c09207c`..`19fdc7c` |
| 4 | `astra_assign_seed` binds the first record, joins the rest | `f0a4492` |
| 5 | The Kit's loop, `astra_vfs_assign_open` (new `vfs_union.c`) | `3fbeeeb`..`5f03d68` |
| 6 | `COMMANDS:` bound as a union; the shell's member walk (folded in — see §4) | `6a0dc70` |
| 7 | Listing, `assign` builtin; two Criticals | `9d8e369`..`58d3327` |
| 8 | `which`; the cross-process proof; the gate at 32 | `a6c5f3a`, `cd914c9` |

Every kernel and userspace gate is green: kernel `make test` at both trace
floors and `K1_QUALIFICATION=1`, userspace `make test`/`sanitize`/`analyze`,
`ext4-test`, and the terminal gate at 32 of 32.

---

## 2. The ABI

`AstraLaunchGrant` and `AstraStartupCapability` both went from 28 to 92 bytes
and both now carry `char root[ASTRA_CAPABILITY_ROOT_MAX]` as their last
field. `ASTRA_CAPABILITY_ROOT_MAX` is 64 and userspace's
`ASTRA_ASSIGN_ROOT_MAX` (`sw/userspace/vfs/include/astra/vfs_assign.h`) is
defined as that same constant — one number, one `_Static_assert`, never two.
The root is normalised, mount-relative, no leading separator: `""` for the
mount's own root, `"local/commands"` for a directory inside it. **The kernel
copies it (`astra_capability_root_set`) and never reads it — the same
contract `flags` already has.** What a name means is the launcher's
statement to the child, not the kernel's business.

`ASTRA_STARTUP_ABI_VERSION` went 1 to 2. That bump was a coordinator
instruction given during execution and appears in no plan or spec —
`docs/ABI.md` is where it is now traceable, and this line is the second place.

`ASTRA_LAUNCH_GRANT_MAX` went 6 to 8, because a two-member `COMMANDS:` is a
seventh grant and the shell already used all six. `sw/kernel/process.h`'s
`KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX` is a **textual alias** of it, not a
second number — the two disagreeing once already let a launch of more than
four grants fail with `INVALID_ARGUMENT` from inside the loader, naming
neither the grant nor the reason, latent for four tasks (`docs/HANDOVER-launch.md`
§4). Task 2's own attempt to guard against a repeat regressed into a
tautology (`X == X`, unable to fail) and was replaced with a
`_Static_assert` — see the plan's task 2 "build settled" block.

Full syscall/record detail is `docs/ABI.md`'s "Process startup ABI 2" section.

**Measurements, worth recording because the next person to widen an ABI
record needs them:**

- `astra_boot.bin`: **256,008 of a 262,144-byte budget**, about 6,136 bytes
  (6 KiB) of ROM headroom after both capability records grew. No
  `docs/MEMORY_MAP.md` ceiling change was needed — the headroom absorbed it.
- Supervisor stack for `ASTRA_SYSCALL_PROCESS_CREATE`, measured with
  `-fstack-usage`: **3,436 bytes worst case** (winning branch
  `prepare_thread`, mapping a new thread's kernel-visible stack pages)
  **against an 8,192-byte stack**, projected to **3,988 bytes** at
  `ASTRA_LAUNCH_GRANT_MAX` = 8 — 4,204 bytes of headroom either way.

---

## 3. What a union is, mechanically

A member is **a repeated name in the assign table** — no `members[]` array,
no second struct. `ASTRA_ASSIGN_MAX` (16) now bounds members rather than
distinct names.

- `astra_assign_bind` drops **every** existing member of a name before
  making the new one — a name has one meaning at a time, and that meaning
  may now be a list.
- `astra_assign_join` appends one member to a name that already exists, and
  refuses `NOT_FOUND` for a name that does not — joining is not a way to
  create a binding.
- `astra_assign_member(table, name, index)` is the `member`'th binding of a
  name, or `NULL` once the index passes the last one — what ends a caller's
  loop. `astra_assign_lookup` is `astra_assign_member(..., 0)`.
- `astra_assign_unbind` removes every member of a name by a **stable
  shift-down**, not swap-compaction — swap-compaction silently reorders a
  union's surviving members, which matters now that order is a property of
  the table (see the plan's task 3 "build settled" block for how the old
  test failed to notice).
- `astra_assign_resolve` gained a `member` index parameter and **stays a
  pure string operation, no I/O** — it answers per member and returns
  `NOT_FOUND` once the index passes the last one. Rights are checked per
  member.

**The Kit does the trying.** `astra_vfs_assign_open`
(`sw/userspace/vfs/src/vfs_union.c`, a new translation unit — `vfs_client.c`
is linked into a host test with no `vfs_assign.c`, so the loop could not live
there) loops `astra_assign_resolve` from member zero, asks a caller-supplied
`client_for` callback which client serves that member, tries the open, and
stops at the first that answers. It remembers the **first non-`NOT_FOUND`
status**, not the last status seen — a member whose device is genuinely
failing must not be reported as "not found" by a later member's ordinary
miss (plan task 5, and commit `5f03d68`). Shell and launched child call the
identical function, which is what makes a union cross a process boundary at
all rather than being a shell-only feature.

**Seeding.** `astra_assign_seed` walks a published capability table in
order: the first record of a name binds, every later record of that name
joins. Order in the capability table is order in the namespace — the grant
array is the authority manifest for a child's search order as well as its
authority.

**The machine's one union.** `COMMANDS:` is bound `local/commands`
read-write first, then joined with `commands` read-only — in
`bind_standard_assigns` (`sw/userspace/supervisor/src/vfs_host.c`). Both
members are made if missing and **omitted, not fatal, if the volume
refuses**; a refusal is said once as a warning event (§6). The writable
member first is the useful case: shipped content a person's own directory
overrides, and the same order that is tried for reading is where a new file
lands.

**The shell.** `launch_path` tries `APPS:`, then every member of
`COMMANDS:` in order, first that opens wins, recorded as an event.
`command_ls` on a union lists every member's contents, **undeduplicated**,
each row tagged `[member]` — a name on two members shows twice. `assign`, a
new read-only builtin with no arguments, prints every name, its members in
order, each member's rights and root.

**`which`** (`sw/userspace/commands/which/which.c`) is a launched program
that holds `COMMANDS:` as two grants with two roots, seeds its own
`AstraAssignTable` from them, and resolves through `astra_vfs_assign_open` —
the cross-process proof that a union is a binding a child inherits, not a
shell feature.

---

## 4. The plan defect

Binding the union and teaching the shell to walk its members were planned as
two tasks (6 and 7). They are one: the moment `COMMANDS:` gained
`local/commands` as an empty member 0, every bare command name became "not a
command" — including `status` and `events`, which had only ever had one
member before this — because `launch_path` still resolved only member 0. The
gate caught it at **10 of 22**. Task 7's step 1 (the member loop in
`launch_path`) was folded into task 6, and
`docs/superpowers/plans/2026-08-07-union-assigns.md` was amended in place to
record the fold-in — read task 6's preamble and its "build settled" block for
the detail, including a second hardcoded member-0 site the original plan
never named.

---

## 5. Two Criticals, both in `command_write`, both found by a gate line
   written red first

**An open is not an existence test on this machine.** `command_write` probed
for an existing name by opening `WRITE|TRUNCATE` with no `CREATE`, reasoning
that truncate-without-create refuses when the file is absent. False here:
the ext4 backend's `mode_of()` maps `ASTRA_VFS_OPEN_TRUNCATE` to lwext4's
`"wb"` whether or not `CREATE` is set, and `"wb"` is `O_CREAT | O_TRUNC` — so
the probe always succeeded at the union's writable primary, creating an
empty file there and never reaching the member that actually held the name.
A person editing a shipped file got a silent shadow copy instead of their
edit. Fixed (`a1f997d`) by locating the holder first with `shell_locate`,
exactly as `rm` already did, and opening only where found (or on the
primary, with `CREATE`, when nothing holds the name at all).

**Existence and permission are different questions.** The fix above made
`shell_locate` resolve every member with the *operation's own* rights, so a
read-only member refused with `ACCESS` before any I/O happened — telling the
walk nothing about whether that member held the name at all. The
worst-status rule (§3) then let that `ACCESS` outrun a later member's honest
`NOT_FOUND`, so **every `write COMMANDS:NEWNAME` was permanently refused**.
A member refusing on rights alone has told you nothing about whether the
file is there. Fixed (`58d3327`) by resolving every member for existence
only, with `READ` — the same right `cd` and `cat` already use to find a name
— and checking the operation's actual rights only against the member that
holds it.

---

## 6. The flakiness, and its honest status

This section needs care: what follows is deliberately split into what is
*verified* and what is not, because a handover that claims a root cause
nobody proved is worse than one that says which part is still open.

**Verified.** `ASTRA_INPUT_READ_OVERFLOW` had exactly three references in
the whole tree — its definition, the kernel setting it, and the kernel's own
test — and `pump_once` (`console_shell.c`) read the flags word from
`ASTRA_SYSCALL_INPUT_READ_TRY` and never looked at that bit, so a dropped
keystroke was unreportable by construction. Separately, `test-terminal.py`
typed the next scripted line as soon as the *previous line's output*
appeared on screen, not when the shell had actually returned to its prompt —
for a command that launches a program, the expected text is often the
child's own output, printed while the shell is still waiting on it, so keys
typed into that window queue up rather than being read as they arrive. That
is a race independent of whether input ever actually overflowed.

**Both are fixed.** The shell now records a dropped keystroke as a
warning-level event (`e7c71a1`) — no count, because the count exists only
inside the emulator model and no register carries it out, and claiming a
number that cannot be known would be worse than the silence it replaces. The
gate now waits for a **bare prompt row, matched exactly rather than as a
substring** (`133354f`), before typing the next line, and asserts at the end
of every run that no input-overflow event was recorded during the boot.

**Not verified.** That input overflow caused the one flake originally
observed. Three reproduction attempts were made: zero key cadence, zero
cadence with the back-pressure wait removed, and a forty-fold burst. The
first two passed clean every time; the third garbled input so badly the run
died before any assertion could fire. None of the three reproduced the
original symptom. What *is* established is that the flag was unreportable
and the gate had a real timing race — either is a legitimate bug worth
having fixed regardless of whether it was the one that fired originally.
20/20 runs clean since, gate 25/25 at that point, but that is evidence the
fixes did not regress anything, not proof of the original root cause.

---

## 7. The missing-member evidence

Not in any gate run — it needs a volume the machine cannot repair, so it was
run by hand (plan task 8, step 7). **Deleting `/local/commands` does not
test this**: the supervisor makes the member if it is missing, so a deleted
directory comes back and nothing is ever skipped. The member has to be one
`mkdir` refuses, which means doctoring `/local` into a regular file **after**
a normal install (reconstructing this recipe cost several wrong turns —
recorded here so the next person does not repeat them):

```sh
cp /tmp/part-clean.img /tmp/part-blocked.img
printf 'not a directory' > /tmp/blocker
# The volume is lifted out, worked on, and put back -- debugfs cannot reopen
# an image?offset= target after a journal recovery, the same reason
# astra_image.py does it this way.
dd if=/tmp/part-blocked.img of=/tmp/vol-blocked.img bs=512 skip=10240 count=120832
e2fsck -fy /tmp/vol-blocked.img
debugfs -w -R "rm /local/commands/devices" /tmp/vol-blocked.img
debugfs -w -R "rmdir /local/commands" /tmp/vol-blocked.img
debugfs -w -R "rmdir /local" /tmp/vol-blocked.img
debugfs -w -R "write /tmp/blocker local" /tmp/vol-blocked.img
dd if=/tmp/vol-blocked.img of=/tmp/part-blocked.img bs=512 seek=10240 conv=notrunc
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part-blocked.img --verbose
pkill -f qemu-system-m68k
```

The scripted gate fails at the three lines that need the writable member,
which is correct and expected against this image. What the run proves, read
from the screen or `--verbose` output:

- `assign` shows `COMMANDS: [0] r  /commands` — **one member**, and the name
  did not vanish with the member that could not be made.
- `which status` answers `/commands/status [0]` — a launched child still
  resolves through the survivor, which is the recoverable failure block
  spanning does not have.
- The supervisor's warning appears **exactly once**:
  `COMMANDS: local member skipped, mkdir refused with status 2
  (src/vfs_host.c:112)`. One line, not one per lookup — a name that silently
  returns less than it did yesterday is worse than one that says why, and
  one that says why on every open is noise.

---

## 8. Traps that have each cost real time

In addition to the ones in `CLAUDE.md` and `docs/HANDOVER-launch.md` §4,
which all still apply.

- **An open is not an existence test on this machine.** `mode_of()` in the
  ext4 backend maps `ASTRA_VFS_OPEN_TRUNCATE` to lwext4's `"wb"` regardless
  of whether `CREATE` was asked for, and `"wb"` is `O_CREAT | O_TRUNC`. Any
  future code that wants to know "does this name exist" must locate first
  (`shell_locate`'s pattern), never open-with-truncate-and-see.
- **Existence and permission are different questions, and have to be asked
  in that order.** Resolve for existence with `READ` first; check the
  operation's actual rights only against the member that holds the file.
  Asking with the operation's own rights makes a read-only member's honest
  refusal indistinguishable from "this file is not here anywhere."
- **Swap-compaction reorders a union.** Any future removal path over the
  assign table must shift survivors down, not swap the last entry into a
  vacated slot — order is now a property of the table, not an implementation
  detail. The task-3 "build settled" block records how the old test failed
  to notice this for two review passes.
- **Two constants for one ABI limit, again.** `ASTRA_CAPABILITY_ROOT_MAX`
  and `ASTRA_ASSIGN_ROOT_MAX` are one number (the kernel's, aliased by
  userspace) for the same reason `ASTRA_LAUNCH_GRANT_MAX` and
  `KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX` are — look for a third if one
  ever gets added without reading this file.
- **Seven kernel tests hardcoded a memory layout the last time an ABI record
  moved** (the launch milestone). Task 1 here moved one again; if a failure
  in this milestone looks unrelated to grants, check what it assumes about
  `ASTRA_STARTUP_CAPABILITY_SIZE` before assuming a real bug. Nothing broke
  this time, but the shape of the risk is the same.
- **A stale command image is invisible.** `make all` in `sw/userspace` does
  not build `commands/`. Every time `which` (or any command) behaves like a
  program you did not write, run `strings build/m68k/which | grep ...`
  before debugging the kernel.

---

## 9. Working on this machine

Same as `docs/HANDOVER-launch.md` §5 — ship from the repo root, always
rebuild the boot image after userspace, two kernel builds
(`make`/`make ASTRA_BUILD=debug`), and the same clean-volume repair recipe.
Repeated here only where the terminal gate itself changed:

```sh
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1
cd sw/kernel && make test \
    HOST_EXTRA_FLAGS="-DKERNEL_TRACE_BUILD_LEVEL=KERNEL_TRACE_LEVEL_DEBUG"
cd sw/userspace && make test && make sanitize && make analyze && make all
cd sw/userspace/commands && make          # separate step -- make all does not build this
cd sw/userspace/storage && make ext4-test
cd sw/boot && make astra_boot.bin
cp /tmp/part-clean.img /tmp/part.img
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img --verbose
pkill -f qemu-system-m68k
```

**The terminal gate is 32 of 32**, up from 22 at the start of this
milestone (25 after task 7's Criticals, 32 after task 8). It now also
asserts that no input overflowed during the boot (§6), and a module-level
assertion beside `SCRIPT` in `test-terminal.py` pins its own last entry to
`("events --boot -1", "the store is RAM")` — the exit-order check depends on
that line being last so its output is still on the 30-row screen when the
check reruns; moving it without updating the assertion now fails immediately
and by name instead of as a scroll-dependent flake (`cd914c9`, the second
time this exact thing happened).

On the Mac: `python3 -m pytest tools/tests/` and
`python3 -m pytest sw/boot/tests/`. Reap QEMU after every gate.

---

## 10. Still open

- **`ext4_backend_open` calls `ext4_fopen` unconditionally when
  `ASTRA_VFS_OPEN_DIRECTORY` is not passed.** What lwext4 does when that
  path names a directory was never verified. This predates this milestone
  and nothing here changed it; flagged in task 7's "build settled" block for
  whoever picks it up.

---

## 11. After this milestone

**The events store is RAM.** `EVENTS:boot/-1` does not exist, and the
terminal gate's last line (`events --boot -1` → `"the store is RAM"`, §9)
already asserts the refusal so nobody mistakes it for a missing feature
rather than a known boundary. Durability is the next queued work — the same
status `docs/HANDOVER-launch.md` §7 already recorded before this milestone
started; nothing here changed it.
