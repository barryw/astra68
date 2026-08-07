# Astra union assigns, and the root a grant was missing

Date: 2026-08-07
Status: design. Nothing here is built.

Depends on `2026-08-06-filesystem-layout-design.md` §1.7 (approved 2026-08-07,
which states the rules a union must obey), §2.5 (command lookup) and §1.4
(identity is the object, never the string), and on
`2026-08-07-program-launch-design.md` (the grant this widens).

## Why

Two things, and the smaller one is the reason the larger one cannot work yet.

**A grant carries no root.** `AstraStartupCapability` is
`{name, handle, rights, flags}`. Every binding a child makes from it is
therefore at its mount's own root, which `vfs_assign.h` says out loud and defers
to "the first grant that needs it". So a launched child holding `COMMANDS:`
resolves `COMMANDS:status` to `/status` — the volume root — rather than to
`/commands/status`. Nothing has hit it because no child has resolved through a
mount assign yet: `events` reaches `EVENTS:`, which is a synthetic mount whose
root genuinely is its root. It is a live defect, not a gap, and the first child
to open a file by name eats it.

**A command's name must be bare.** §1.7's trigger is met: a person who installs
a program somewhere the system did not ship it wants to type its name, and
"spell the category" is exactly what a bare name exists to avoid. That needs one
name to reach two places, which is a union.

The first is the prerequisite for the second. A union that cannot cross a launch
is half a mechanism, and the half it is missing is the half a person uses.

---

## 1. What a union is

### 1.1 A member is a repeated name

An assign table entry already carries everything a member needs: a handle,
rights and a root. A union is therefore **two entries carrying the same name**,
and their order in the table is the order they are tried in.

No `members[]` array, no second struct, no new memory. The alternative — an
array inside `AstraAssign` — makes every assign pay for a union it does not have
(sixteen entries times four roots is 4.6 KB against today's 1.4 KB), and it
still cannot be expressed in a launch grant without inventing a second encoding
for the same idea. A repeated name needs no second encoding: the shell grants
`COMMANDS` twice.

`ASTRA_ASSIGN_MAX` (16) now bounds members rather than names. That is the honest
reading of the number: it was always "how many bindings a process may hold", and
a member is a binding.

### 1.2 Binding replaces, joining appends

```c
uint32_t astra_assign_bind(AstraAssignTable *, const char *name,
                           uint32_t handle, uint32_t rights, const char *root);
uint32_t astra_assign_join(AstraAssignTable *, const char *name,
                           uint32_t handle, uint32_t rights, const char *root);
```

`astra_assign_bind` keeps the meaning it has: a name has one meaning at a time,
so binding drops **every** entry of that name first. What changes is only that
the meaning may now be a list. `astra_assign_join` appends one member to the end
of a name that already exists, and refuses `NOT_FOUND` for a name that does
not — joining is not a way to create a binding, because a member joined to
nothing is a name whose first member is an accident of ordering.

`astra_assign_unbind` removes every member of the name. Removing one member is
not in this milestone: nothing needs it, and the shape it wants (by index, or by
handle and root) is a decision better made by the thing that needs it.

### 1.3 Resolution answers per member

```c
uint32_t astra_assign_resolve(const AstraAssignTable *, const char *path,
                              uint32_t rights, uint32_t member,
                              char *wire, uint32_t capacity,
                              const AstraAssign **assign);
```

The one new parameter is `member`. Resolution answers for that member and
returns `ASTRA_VFS_ERR_NOT_FOUND` once the index passes the last one, which is
what ends a caller's loop.

**Resolution stays pure**, which is the whole reason for the index. The tempting
implementation is to stat each member until one answers, and that drags I/O into
the one layer whose value is having none: today `astra_assign_resolve` is a
string operation tested with no filesystem anywhere near it, and it stays that
way. The Kit does the trying, because the Kit is already where the I/O is.

Rights are checked **per member**, and a member that lacks the requested right
refuses exactly as an assign lacking it refuses today. A caller looping for
write therefore skips read-only members and lands on the first writable one,
which is §1.7's "creation goes to the primary" — not a stored field, but a
consequence of a fixed order. A person can still answer *which disk is this file
on* by reading the member list, which is what the rule is for.

### 1.4 A caller holding the index knows which member answered

The index is the answer to "which one ran". It is not derived, not looked up
afterwards, and not a string comparison: the loop that found the file is holding
it. §2.5 requires the launch to record it, and the launch has it.

---

## 2. What a grant carries

### 2.1 A root, sixty-four bytes, copied and never read

```c
typedef struct AstraLaunchGrant {
    char     name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t handle;
    uint32_t rights;
    uint32_t flags;
    char     root[ASTRA_ASSIGN_ROOT_MAX];   /* new */
} AstraLaunchGrant;
```

and the same field on `AstraStartupCapability`. Both go 28 bytes to 92, both
keep their `_Static_assert`, and both are an ABI change that `docs/ABI.md`
records.

Sixty-four because that is `ASTRA_ASSIGN_ROOT_MAX` exactly. A shorter root in
the grant would halve the block and buy a truncation rule that has to be refused
somewhere and explained forever, for about 250 bytes a process.

The root is **normalised, mount-relative, no leading separator** — the form
`AstraAssign.root` already stores, so seeding is a copy rather than a parse. The
kernel copies it and never reads it, the same contract `flags` has: the kernel
carries what a name means and does not decide it.

Refusals at the syscall, all `INVALID_ARGUMENT`: a root field with no NUL in it,
a leading `/`, and any `..` component. The last is not the `..` rule — that is
resolution's, and unaffected — but a grant is where a root enters the system
from outside, and a root that climbs is not a root.

`ASTRA_STARTUP_CAPABILITY_MAX` stays 32, so the published block goes 896 bytes
to 2,944 at the ceiling and about 550 for a process publishing six. Trimming the
ceiling is a second decision and does not ride along here.

### 2.2 Eight grants, one number

`ASTRA_LAUNCH_GRANT_MAX` goes 6 to 8, because a two-member `COMMANDS:` is a
seventh grant and the shell already used all six.
`KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX` moves with it, as the single number
the launch milestone's `_Static_assert` made them. That assert is the guard
against re-opening the gap where a launch of more than four grants failed with
`INVALID_ARGUMENT` from inside the loader, naming neither the grant nor the
reason, and stayed latent for four tasks.

### 2.3 Seeding binds the first and joins the rest

`astra_assign_seed` walks the published table in order: the first record of a
name binds, every later record of that name joins. Order in the capability table
is order in the namespace, which makes the grant array the authority manifest
for the child's search order as well as for its authority — one list, read once,
meaning one thing.

Everything else about seeding is unchanged, including the positive rule: only a
grant carrying `ASTRA_CAPABILITY_FLAG_NAMESPACE` becomes a name at all.

---

## 3. What the machine does with it

### 3.1 The members

`COMMANDS:` is bound to `local/commands` read-write and joined with `commands`
read-only, both on the one volume this machine has, and **that is the order they
are tried in**.

The writable member first is §1.7's stated useful case rather than a
demonstration: shipped content that a person's own directory overrides, which is
`DEFAULTS:`/`CONFIG:` layering at the namespace level instead of inside one
reader. It also makes the primary for creation the first member, so the order a
person reads and the place a new file lands are the same line rather than two
rules to hold together.

It follows that a file dropped into `local/commands` shadows a shipped command
of the same name. That is what override means, and `APPS:` already accepts it
one place earlier for the same reason. The protections are the ones §1.7 states
and this design keeps: the member was joined explicitly by the startup sequence
and not by a disk appearing in a slot, the shadowing is visible in a listing
because nothing is deduplicated, and the launch records which member answered.

`local/commands` is made if missing and omitted rather than fatal if the volume
refuses, exactly as `WORK:` and `COMMANDS:` are today. A member that is absent
is skipped and **said once**, as an event — the union keeps working, and a name
that silently returns less than it did yesterday is worse than one that says
why. Joining is a line in the supervisor's own startup sequence, which is §3.2's
authority manifest; it is never a coincidence of label, and no disk appearing in
a slot joins anything.

When a second volume exists, a member gains a different handle and nothing else
in this design moves.

### 3.2 The loop lives in the Kit

```c
uint32_t astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                               uint32_t rights, AstraVfsAssignClientFn client_for,
                               void *context, AstraVfsFile *file,
                               const AstraAssign **assign, uint32_t *member);
```

It loops `astra_assign_resolve` from member zero, asks `client_for` which client
serves that member, tries the open, and stops at the first that answers. Past
the last member it returns what resolution returned, which is `NOT_FOUND`.

`client_for` is a callback for the same reason the transport is one: the
supervisor maps an assign to one of several clients, a child has one client per
handle, and neither should be a special case inside the Kit. Shell and child
call the identical function, which is the property that makes a union cross a
process boundary at all.

### 3.3 The shell

**Lookup.** `launch_path` tries `APPS:`, then every member of `COMMANDS:` in
order, first that opens wins, and it keeps the member index. §2.5 is unchanged
by this: `APPS:` stays a separate first place ahead of the union rather than
becoming a member of it, because the distinction it draws is deliberate.

**Listing.** `ls` on a union enumerates every member in order and does **not**
deduplicate. A name on two members is shown twice, with the member it came from.
A duplicate-name set would be memory proportional to the directory, which every
enumeration on this machine refuses, and hiding the loser makes a listing
disagree with what a lookup would do — the confusing failure where a person sees
one directory and a different program runs. `readdir` has been a cursor since
`78965bd`, so the cost is a loop and a column.

**`assign`**, a new builtin with no arguments: every name, its members in order,
each member's rights and root. It is read-only. Joining at the prompt is a shell
language decision the layout spec defers, and nothing here needs to rebind at
runtime.

It is a builtin rather than a program because it prints the *shell's* namespace,
and a launched program holds its own — a program asking this question would
truthfully answer about itself and be read as answering about the prompt.

---

## 4. What proves it

**Terminal gate.**

- The same command name installed on both members with different output: the
  writable member's runs, shadowing the shipped one, and the launch says which
  member answered.
- `ls COMMANDS:` shows both, each against its member.
- A launched child granted the two-member `COMMANDS:` opens a file by bare name
  from the second member. This is the roots-in-grants fix and the union crossing
  a process boundary, in one assertion.
- With `local/commands` removed, the union still answers from the survivor and
  exactly one event records the skip.

**Host tests**, in `test_vfs_assign.c`, with no filesystem anywhere near them:
member index past the last returning `NOT_FOUND`; per-member rights refusing a
write to the read-only member; the first writable member being the one a write
lands on; `bind` dropping every member of a name; `join` on an unbound name
refusing.

**Kernel tests**: a grant carrying a root arriving at the child with that root;
the three root refusals; and eight grants accepted where nine are refused.

## 5. What this is not

A union assign has the shape of a search path and none of its substance. A path
variable is a string any program can rewrite, so anything that can set it can
decide which program runs. A union is a binding: its members are joined by the
startup sequence, they carry their own rights, and a program cannot extend the
list by naming anything. The question `PATH` cannot answer — which one ran — is
answered by an index the loop is already holding.

Block-level spanning stays refused, for the reasons §1.7 gives: it makes two
media one failure domain, and on a machine with removable media a design where
pulling a card corrupts the boot volume is wrong on its face.

## 6. Deliberately not in this milestone

- **Removing one member.** Nothing needs it and its shape should be decided by
  what does.
- **Joining at the prompt, and `CONFIG:startup`.** A startup-script reader is a
  milestone wearing this one's name.
- **A second volume.** More room is a second name; this design already survives
  one arriving.
- **`ASTRA_STARTUP_CAPABILITY_MAX`.** Widened fields only; the ceiling is a
  separate decision.
