# Astra namespace and filesystem layout

Date: 2026-08-06
Status: design, approved in conversation. §1.1–§1.4 are built; everything else
is not. §1.6 and §1.7 were added after the first three tasks landed, to answer
how a second volume is named and what it means to join one to a name that
already exists.

Scope: what names exist, what they point at, what the system sees at startup,
where configuration lives, and the rules that keep a person from breaking the
machine by editing a file. The event and log system is a separate spec; this
one fixes only where its store lives and settles one ordering question that
would otherwise be decided by accident. The shell language is a separate spec
and nothing here depends on it.

## Why this shape

Astra is a purpose-built single-user machine, which is the Amiga's situation
and not Unix's. It is also capability-based, which is neither's. The layout
follows from taking both facts seriously rather than importing a hierarchy
designed for time-shared minicomputers with sixteen users and one disk.

Three properties are the point:

- A program can only name what it was handed.
- The system's integrity never depends on the correctness of anything a person
  can edit.
- A power user can bend the machine without being able to brick it.
- **There is one way to do each thing.** Linux's flexibility is its most
  expensive feature: configuration lives in four places, in three formats, with
  two mechanisms to enable it, and the cost is paid by every person who has to
  work out which one is in force. A 68030 with 32 MiB does not get to afford
  that, and would not want it if it could. One place, one format, one
  mechanism, every time — and where something is deliberately not offered, the
  reason is written down rather than left as an absence.

---

## 1. The model

### 1.1 An assign is a granted binding

`SYS:`, `COMMANDS:`, `WORK:` are entries in a process's capability table:
`name -> (mount handle, rights)`, handed over at launch. This is the table that
already delivers `PROC`, `BLKD`, `DSPD` and `INPD` to the supervisor; it grows
a string name and becomes the namespace.

```c
typedef struct AstraStartupCapability {
    char     name[16];   /* "COMMANDS", "WORK", canonical uppercase */
    uint32_t handle;
    uint32_t rights;
    uint32_t flags;
} AstraStartupCapability;
```

A process may create private assigns over mounts it already holds. It cannot
invent authority by naming: an assign is a name for a capability, never a way
to obtain one.

**There is no global namespace.** Nothing enumerates all mounts, and nothing
resolves a name the caller was not given. `df` and a mount list are answers a
mounter service gives to a caller holding the right to ask, in the same way
`OBSERVABILITY.md` already decided `PROC:` is a view rather than an ambient
directory.

### 1.2 Every absolute path is `NAME:rest`

There is no `/`. The absence of a root is a security property rather than an
aesthetic one: there is nothing to enumerate, and `..` at an assign's root is
an error rather than a parent, so no string a program can build escapes the
authority it was given.

`/` exists only inside the POSIX personality, which `TERMINAL_AND_POSIX.md`
already places in userspace and already gives ownership of paths. The
personality maps `/` onto granted assigns for ported software. The native
namespace never has a root.

### 1.3 Assign names are case-insensitive; the rest of the path is not

`sys:` and `SYS:` are one binding, canonicalised uppercase. Assign names are a
small closed set typed by humans, and a typo must not create a second
namespace.

Everything after the colon stays byte-exact, as `STORAGE_AND_VFS.md` locks:
`Makefile` and `makefile` are two files. The two rules live in one string and
that is worth one sentence of teaching.

### 1.4 Identity is the object, never the string

Assigns alias. `WORK:` and a private `PROJ:` may name the same directory, so
two different strings name one object. Any code asking "is this the same file"
compares object identity, which the handler contract already provides. Nothing
may compare paths to answer it.

### 1.5 Mounting is naming

One kind of name, not the Amiga's three. Mounting yields a handle; binding a
name to it is creating an assign. There is no device namespace and no separate
volume namespace.

A binding records the volume's identity — label, uuid, media generation — so a
binding whose volume is absent fails with *that volume is not present* instead
of resolving to whatever occupies the slot. Silently resolving to the wrong
disk is the Unix mount-point mistake and the block layer already tracks the
generations needed to avoid it.

### 1.6 A second volume is named by its label, never by its slot

There is no `DH0:`, `DH1:`, `DH2:`. Numbering volumes by the order they were
discovered is `/dev/sda` with a colon on the end: plug a card reader in before
power-on and the disk renames itself, a `CONFIG:` line naming `DH1:` means a
different disk on a different machine, and after a hot-plug nobody can say which
disk they are looking at. §1.5 exists to prevent exactly that, and a slot number
would reintroduce it as a naming convention.

**A volume's label is its name.** A volume labelled `PHOTOS` mounts as
`PHOTOS:`. Move the disk to another Astra and it is still `PHOTOS:`, because the
name travelled with the medium rather than with the socket. This is the one part
of the Amiga's three namespaces worth keeping: its *volume* name followed the
disk, and only its *device* name named the socket.

| The Amiga had | Astra |
|---|---|
| `DH0:`, the device — a socket | nothing. A drive is a block capability, not a path; you format the drive you hold a handle to, and it has no name in this namespace at all. |
| `Work:`, the volume — a label | kept, and it is now the only way a mount acquires a name |
| `SYS:`, `LIBS:`, the assigns | kept, and now the same mechanism as the above |

The boot volume is `SYS:` **by role, not by label**: it is the volume the system
was started from, which is the sense the Amiga's `SYS:` also had. Its label is
whatever it happens to be, and nothing may depend on that label being any
particular string.

Three cases, each with one answer:

- **A volume with no label** is not bound. The mounter reports what it saw to a
  caller holding the right to ask, and a person binds it by naming it. Nothing
  becomes reachable without someone saying so, which is §1.1's rule and not a
  new one.
- **Two volumes with the same label.** The first bound wins; the second is
  reported and left unbound rather than silently shadowing it. The label is a
  name and the uuid is the identity (§1.4), so this is a name collision and is
  answered like one.
- **A bound volume that leaves.** The binding stays and fails as *that volume is
  not present*. Media generation is already tracked by the block layer.

### 1.7 A volume is never joined to another volume

Two things could be meant by "extend `WORK:` with a second disk". One is
permanently refused and one is designed and deliberately not built.

**Refused: block-level spanning.** Concatenating two devices under one
filesystem — LVM, a device-mapper stripe, a multi-device
filesystem — is refused for this machine:

- ext4 does not span devices, so it would mean a volume manager underneath
  lwext4, on a 68030, on a bounded allocator. The cost is enormous and the
  benefit is that one number is larger.
- It makes both media **one failure domain**. Remove the second disk and the
  whole volume is destroyed, including everything that was on the first one.
  On a single-user machine with removable media, a design where pulling a card
  corrupts the boot volume is wrong on its face, and it contradicts §7 and §3.5
  outright.

**Union assigns: designed here, and now approved for `COMMANDS:`.** An assign
names an ordered list of `(mount, root, rights)` rather than one of them, with
lookup trying each member and the first hit answering — the Amiga's
multi-directory assign. That has the recoverable failure block spanning does
not: a member that is gone takes only the files that were on it, and every
other member still answers.

**Amended 2026-08-07: the trigger below is met.** A person who installs a
program somewhere the system did not ship it wants to type its bare name, and
"spell the category" is exactly the thing a command's name being bare exists to
avoid. Commands are therefore a collection that genuinely cannot be split by
name, which is what §1.7 said to wait for. The prerequisite this section named
— `readdir` becoming a cursor — was paid in `78965bd`.

It is built for `COMMANDS:` first. A second *volume* still gets a second name:
more room remains a second name, and nothing about this amendment changes that.

The reason is that no part of this machine needs one name to reach two volumes.
More room is a second name — `PHOTOS:` — which is what an assign is for, and
two names cost nothing to implement and nothing to explain. Meanwhile the two
places where a search order genuinely exists, command lookup in §2.5 and kit
lookup in §4.1, are both **fixed two-element lists with a stated order**, both
hardcoded where they are used. A general union would be a third implementation
of something that already exists twice in a simpler form, and its cost is not
the lookup loop — that is a few lines — but everything around it: `readdir`
becoming a cursor, a way to ask which member answered, and a shadowing rule a
person has to hold in their head.

**The trigger.** When one logical collection genuinely cannot be split by name
— when something other than a person's taste requires a single name to span
media — this is the design to build, and the rules below are what it must obey.
Running out of room on one disk is not that trigger; a command whose name must
be bare is.

**What this is not.** A union assign has the shape of a search path and none of
its substance, and the difference is why it is allowed here while `PATH` is
not. A path variable is a string any program can rewrite, so anything that can
set it can decide which program runs. A union is a *binding*: it is joined by a
line in `CONFIG:startup` or by a command a person ran, its members carry their
own rights, and a program cannot extend the list by naming anything. The
question `PATH` cannot answer — which one ran — is answerable here, and the
launch records it.

The rules it would have to obey:

- **A member is joined explicitly, never by coincidence of label.** A disk
  labelled `work` appearing in a slot must not silently join `WORK:` — that is
  a mechanism for handing someone a card that shadows the files they read every
  day. Joining is a line in `CONFIG:startup` or a command a person ran.
- **Creation goes to the primary**, which is the first member holding write
  rights and is fixed when the binding is made. Never "whichever has the most
  free space": a person must be able to answer *which disk is this file on*
  without looking.
- **Shadowing is first-hit, and the order is stated.** Two members carrying the
  same name means the later one is invisible. This is the same two-places,
  one-stated-order rule as command lookup in §2.5, and it needs the same thing
  §2.5 needs — a way to ask which member answered.
- **Rights are per member.** A read-only member under a writable union is the
  useful case, not an edge one: shipped content that a person's own volume
  overrides, which is §5's `DEFAULTS:`/`CONFIG:` layering at the namespace level
  instead of inside one reader.
- **A missing member is skipped and said out loud, once.** The union keeps
  working; a name that silently returns less than it did yesterday is worse than
  a name that says why.
- **The `..` rule is unaffected.** Resolution still produces one member and one
  path within that member's root, and no member's root can be climbed out of.
  A union adds candidates, never an escape.

**Resolution stays pure.** `astra_assign_resolve` is a string operation today —
name and path in, wire path out, no disk touched, refusals decided locally, and
tested without a filesystem anywhere near it. A union tempts the obvious
implementation, which is to stat each member until one answers, and that drags
I/O into the layer whose whole value is not having any.

So resolution answers *per member*: it takes the member's index and returns that
member's path, or `NOT_FOUND` once the index passes the last one. The Kit loops
over the candidates and stops at the first that opens, because the Kit is
already where the I/O is. The pure function stays pure, the trying happens once,
and a caller that wants to know which member answered is holding the index that
says so.

**Listing a union.** The cost this section named first — `readdir` addressed by
index, quadratic in the ext4 backend — was paid in `78965bd`; enumeration is a
cursor now. What remains is duplicate names, and the answer is to **not**
deduplicate: each member is listed in order and a name that appears twice is
shown twice, with the member it came from. A duplicate-name set is memory
proportional to the directory rather than to the page, which is the one thing
every enumeration on this machine refuses. Seeing both is also the honest
answer: the shadowing is real, and hiding the loser makes a listing disagree
with what a lookup would do.

Lookup, open, read and write cost one extra attempt per member that does not
answer, and stop at the first that does.

### 1.8 No symbolic links

The native filesystem has none. Assigns are the aliasing mechanism, and links
would add loops, resolution races, and subtree escapes to do a job already
done. A POSIX personality may emulate them within its own view.

---

## 2. What exists

### 2.1 Read-only, on the system volume

| Assign | Holds |
|---|---|
| `SYS:` | the system volume itself |
| `COMMANDS:` | executables the shell finds by name |
| `LIBS:` | shared kits |
| `DRIVERS:` | device drivers and filesystem handlers |
| `SERVICES:` | long-running programs the system starts with authority |
| `FONTS:` | fonts |
| `DEFAULTS:` | configuration as shipped |
| `STARTUP:` | the startup sequence |

`SERVICES:` is separate from `COMMANDS:` because the distinction is authority
rather than taste: a service is started by the system and handed capabilities;
a command is run by a person and inherits theirs.

### 2.2 Writable, on the state volume

| Assign | Holds | Rule |
|---|---|---|
| `CONFIG:` | machine configuration, overriding `DEFAULTS:` | survives system updates |
| `APPS:` | installed applications | one directory each; see §4 |
| `WORK:` | the person's files | the only place they live |
| `TEMP:` | scratch | emptied at every boot |

The state volume also carries `events/`, which is **not** an assign anybody
holds. It is the events service's own bounded store, bound read-write to that
one service and to nothing else; what the rest of the machine reads is the
synthetic `EVENTS:` below. See §6.

### 2.3 Synthetic

`PROC:`, as specified in `OBSERVABILITY.md`, and `EVENTS:`, which renders the
event store at read time — the tree in the event spec's §7.1. Neither is a
special mechanism: both are handlers behind the same node contract as a disk,
and `OBSERVABILITY.md` already requires that adding one needs no special case.

A synthetic tree is bound with the rights that describe it. `EVENTS:` is bound
read-only, so nothing can `rm` an event, and a process that was granted no
binding does not see the tree at all.

### 2.4 The tree

```
SYS:                      read-only mount
  commands/  libs/  drivers/  services/  fonts/
  defaults/               -> DEFAULTS:
  startup/                -> STARTUP:
  version                 what this system image is

<state volume>            writable mount
  config/  work/  temp/
  events/                 the events service's store; no assign names it
  apps/
    Editor/               one directory per application
      manifest  program  libs/  resources/
```

### 2.5 Command lookup

A bare command name is searched in exactly two places, in this order:

1. `APPS:`
2. `COMMANDS:`

`APPS:` first, so a person can replace a shipped command deliberately. Where
`COMMANDS:` is a union (§1.7), its members are tried in their stated order after
`APPS:`, and the resolved member is what the launch records — so the list a
person can read and the answer to "which one ran" are the same thing.

Two entries, one stated order, no configurable search path: a path variable is a
hijacking surface and an unanswerable "which one ran" question, and neither is
worth the flexibility on a machine with two locations.

**`COMMANDS:` may have subdirectories, and they are never searched.** A bare
name resolves against its top level only; a program in a category is named by
its category, `dev/objdump`. A searched tree is a search path with the
configuration file removed and the ordering hidden in the directory layout —
same hijacking surface, same unanswerable question, plus a scan on every
invocation. `2026-08-07-program-launch-design.md` §5 has the reasoning and what
else was considered.

### 2.6 Deliberately absent

`CACHE:` and `STATE:` are not in this design. Nothing generates either yet and
`TEMP:` covers scratch. They are cheap to add when something needs them and
expensive to explain when nothing does.

---

## 3. Startup

### 3.1 What the system sees

1. The ROM validates NVRAM and hands the kernel a boot block, as today.
2. The kernel starts the supervisor with its process, thread and device leases.
3. The supervisor reads the state volume's identity from NVRAM, mounts the
   system volume read-only and the state volume writable, and binds the
   standard assigns.
4. The supervisor runs `STARTUP:`, granting each entry the authority its line
   declares.

Nothing is ambient at any step. From (4) onward, every program's namespace was
handed to it.

### 3.2 The startup sequence is the authority manifest

An ordered file, one entry per line:

```
service SERVICES:events   grants STORE:rw TEMP:rw  serves EVENTS:r  required
service SERVICES:storage  grants DRIVERS:r  CONFIG:r                required
service SERVICES:input    grants CONFIG:r                           required
command COMMANDS:shell    grants WORK:rw APPS:r COMMANDS:r LIBS:r   required
```

One file states what every program on the machine may touch. That is not
answerable on a Unix at any price, and it is answerable here because the
namespace is granted rather than ambient.

Two clauses beyond the first draft, both general rather than events-specific:

- **`STORE:` is a service's own private state**, bound by the supervisor to the
  directory the state volume keeps for that service. It exists only inside that
  service's namespace, so two services both holding `STORE:` hold different
  directories and neither can name the other's.
- **`serves NAME:r`** declares a synthetic tree this service publishes and the
  rights it is published with. `EVENTS:` and `PROC:` arrive this way. It is a
  grant in the other direction — a `grants EVENTS:r` on some later entry can
  only name what a `serves` produced — which is what keeps one file the answer
  to what may touch what.

**A grant whose binding does not exist is omitted, not fatal.** With no state
volume there is no `APPS:` to hand the shell, so the shell starts with a
smaller namespace and naming `APPS:` fails cleanly inside it. An entry fails
only when its own program fails. Without this rule §3.4 would be a promise the
manifest could not keep.

### 3.3 Services

A **service** is a long-running program the system starts with authority it
declares. It is not a command, which runs and exits, and not an application,
which has documents and a single instance. Three kinds, three locations,
one rule each.

**The manifest is the only source of truth for what runs.** There is no runtime
start, stop, enable, disable, or mask. Turning a service off is editing its
line; the change takes effect at the next boot.

This is justified by this machine specifically. The terminal is up **0.08
seconds** after reset, measured, with a gate that fails the build at 1.00s. A
reboot costs less than reading the manual for a service manager, so runtime
manipulation buys almost nothing and costs a state machine — the one where
systemd has *enabled*, *disabled*, *masked*, *static*, *started* and *stopped*,
and no one can say which combination they are in. Here the question "is it
running?" is answered by one file and one boot.

A `service` command shows what is running, what failed to start and why, and
edits the manifest when asked. It has no verbs that change the running set
without changing the file.

**User entries live in `CONFIG:startup`**, appended after the shipped sequence,
and are never `required` — §7.

**A service that dies is reported, not restarted.** Its clients hold handles to
it; restarting silently would hand them a service that does not remember them,
and the kernel already models the honest answer as `PEER_DEAD`. The system
continues with the service absent, which on a single-user machine means the
person is told that storage is gone rather than watching a restart loop.
Automatic restart becomes meaningful when reconnection is designed, and that is
not now.

**There is no registrar.** A client reaches a service through a handle it was
granted at launch, as `CLAUDE.md` already directs. Nothing looks a service up
by name at runtime, so nothing can be impersonated by registering first.

### 3.4 The event sink starts first

It is what records the failures of everything after it. Before it exists,
failures go to the console channel, which needs no service and no disk — and
which stops narrating the moment the service drains the ring, because from then
on it is a second timeline over the terminal's own display. The event spec's
§6.1 has the rule.

### 3.5 A machine with no state volume still boots

Fresh disk, failed disk, first power-on: `SYS:` alone yields a working system.
the event store and `TEMP:` fall back to RAM — `EVENTS:` is served either way,
because the tree is the service and not the disk — `CONFIG:` is absent and
defaults apply,
`WORK:` and `APPS:` are unbound and naming them fails cleanly.

A system that cannot boot because a writable partition is missing has confused
its installation with its existence.

---

## 4. Applications and how they launch

### 4.1 A bundle is a directory

An application is a directory containing everything it needs. Installing is
copying it into `APPS:`; removing it is deleting it. There is no installer, no
registry, no receipt database, and no files left behind in four other places.

```
APPS:Editor/
  manifest          what it is, what it wants, what it opens
  program           the executable
  libs/             kits private to this application
  resources/        icons, layouts, data
```

**A bundle is never written to by the application inside it.** Settings go to
`CONFIG:Editor/`, documents to `WORK:`. That is what makes a bundle replaceable
by copying over it, backed up by copying it, and identical on two machines — an
application that writes into itself is one that cannot be updated without
losing state or updated without carrying state it should not have.

Kits resolve from the bundle's `libs/` first, then `LIBS:`, the same two-place
rule with a stated order as command lookup in §2.5. A bundle can carry a kit
version the system does not have without asking anyone's permission and without
affecting anything else.

The manifest declares the application's name, the authority it wants, whether
it wants a terminal, and which documents it opens. What that file looks like,
and how a person sees and answers an authority request, is the bundle spec —
not this one.

### 4.2 Two launch contexts

A program is launched from the shell or from the GUI, as on the Amiga, and it
is told which. The distinction is not cosmetic: the two contexts hand over
different things.

| | Launched from the shell | Launched from the GUI |
|---|---|---|
| Arguments | `argv` strings, plus a handle for every path argument | the objects it was launched with, as handles |
| Terminal | attached | **none** |
| Namespace | inherits the shell's, broadly | what the manifest asked for and was granted |
| Working directory | the shell's | the bundle's, plus wherever the documents came from |
| Exit status | returned to the shell | returned to the launcher, which must show a failure rather than swallow it |

Two things follow, and both are improvements on the Amiga rather than copies of
it.

**The launch context is stated, not inferred.** An Amiga program worked out
where it came from by testing whether it had a CLI, which meant every program
carried the same fragile deduction. Astra puts it in the startup block as a
field, and a program that does not care never looks.

**Structurally the two contexts deliver the same thing:** a list of granted
objects. Typing `edit WORK:notes.txt` and dropping `notes.txt` on the editor
both hand the editor a handle to that file — the difference is who chose it and
whether a terminal came too. The powerbox and the command line turn out to be
the same mechanism, which is why the editor needs no assigns at all in either
case.

### 4.3 A program must never assume a terminal

Launched from the GUI it has no terminal handle, so writing to one is not a
fault to guard against — there is nothing to write to. Diagnostics go to the
event channel, which exists in both contexts and needs neither a terminal nor a
disk.

This is the fix for the Amiga's worst launch-time behaviour, where a program
started from Workbench wrote into the void and people resorted to a serial
cable to find out why. Here the output is in `EVENTS:` and the machine can be
asked what the program said.

A shell script launched from the GUI starts the shell with the script; whether
a terminal window appears is declared in the manifest rather than guessed.

### 4.4 One instance per application, many documents

An application runs once. Launching a document it already has open, or a second
document, delivers to the running instance rather than starting another copy.
A launch is already a message, so this is routing rather than a new mechanism:
the instance holds a receive port and the launcher sends the launch record to
it.

This is a resource decision before it is a taste one. A second copy of an
editor on a 32 MiB machine costs a second image, a second heap and a second set
of kits to hold one more document, and the multi-document interface every
desktop settled on exists because that trade is a bad one.

The rules that make it work:

- **Commands are not applications.** A command in `COMMANDS:` runs and exits,
  and two invocations are two processes as they have always been. Single
  instance is a property of a bundle, which is the thing with documents, a
  manifest and a lifetime.
- **Identity comes from the manifest**, not from the path a bundle sits at. Two
  copies of the same application are the same application, and the launcher
  runs one of them.
- **Delivery is bounded and its failure is visible.** An instance that does not
  accept a launch within a deadline is reported to the person, who can leave it
  or replace it. A launcher that waits forever on a hung application is a
  desktop that stops responding for a reason nobody can see.
- **A dead instance is replaced, not resurrected.** If the process is gone the
  launcher starts a fresh one; the person does not need to know one had died.

The cost, stated plainly: one crash takes every open document with it. That is
the trade every multi-document system makes, and the alternative -- a process
per document, the way modern browsers isolate tabs -- costs more memory than
this machine has.

---

## 5. Configuration

### 4.1 Two layers

`DEFAULTS:` as shipped, `CONFIG:` overriding it. A reader opens
`DEFAULTS:storage.conf` then `CONFIG:storage.conf`; later keys win. No merge
service: a program that can open two files can do this.

### 4.2 Format

Line-oriented `key = value`, ASCII, one file per subsystem.

**No includes, no conditionals, no substitution, no nesting.** Configuration
that gains a language becomes a program whose behaviour cannot be predicted by
reading it. Anything needing logic is a startup script and says so by being
one.

There is no unset: `key =` sets an empty value. An absent file means defaults.

### 4.3 Parse fully or not at all

A file that fails to parse is ignored **whole**, the subsystem runs on
defaults, and an event records the file and the line. Half-applied
configuration produces a machine in a state nobody can reason about, including
the person who wrote the file.

---

## 6. Where events live

The format, levels and automatic context are the next spec. Two things are
fixed here because they are layout and ordering decisions:

**The store is a bounded on-disk ring.** It has a size budget, it drops oldest,
and it never grows without limit. A machine that will not boot because it
logged too much about not booting is `/var/log` in a nicer hat.

**`EVENTS:` is the view of it, not the bytes.** The store is the state volume's
`events/`, which only the events service holds (`STORE:`, §3.2). What everything
else sees is a synthetic read-only tree the service serves through the ordinary
node contract, so `ls` and `cat` are how history is read and nothing can delete
an event by writing to the namespace. The event spec's §7 has the tree.

**One stream, two sinks.** `OBSERVABILITY.md` requires that program lines and
kernel events share one ordered stream with one set of sequence numbers. The
kernel trace ring is that stream: `ASTRA_SYSCALL_LOG_WRITE` appends there, and
the console is a sink on it rather than a second destination with its own
ordering. The events service drains the ring into `EVENTS:`. This preserves the
console path that works when nothing else does, without creating the second
timeline the existing direction warns against.

---

## 7. Not breaking the machine

The governing rule:

> **The system's integrity never depends on the correctness of anything the
> user can edit.**

| Mechanism | Effect |
|---|---|
| `SYS:` is mounted read-only | A typo cannot reach the system. Not a convention — a mount. |
| User-editable files are advisory | Bad config yields defaults and a loud event, never a half-applied state. |
| Nothing user-supplied is `required` | Shipped startup entries may be required; user-added ones may not. No edit can prevent boot. |
| Failsafe boot | Boots from `SYS:` alone, ignoring `CONFIG:`, `APPS:` and user startup entries. |

Failsafe has two triggers and one meaning. The ROM samples a front-panel button
at reset; the running system can set an NVRAM flag that applies to the next
boot and clears itself once used. Either produces the same boot, so a machine
that cannot reach its own settings is recoverable from the outside and one that
can is recoverable from the inside.

A power user may rebind assigns, add startup services, install applications and
replace a shipped command. The worst outcome of getting it wrong is a machine
that boots to defaults and names the line it refused.

**Last-known-good boot** — reverting the most recent configuration change after
a failed boot — is designed for and not built. NVRAM already carries two slots
with generation numbers and CRC validation, which is most of the mechanism; the
missing piece is a definition of "failed boot" that is not itself a source of
bugs. An NVRAM TLV is reserved for the boot-attempt counter.

---

## 8. Accepted limits

- **Nothing can scan the whole machine.** No `find /`, no indexer, no
  search-everywhere without being handed everywhere. Correct for this machine,
  occasionally annoying.
- **Ported software needs the POSIX personality**, which owns `/` and maps it
  onto granted assigns. Planned rather than improvised.
- **Multi-user is not foreclosed.** `WORK:` is a binding, so a second person is
  a second binding rather than a layout change. The door costs nothing to leave
  open and nothing to leave shut.

---

## 9. What this changes in existing code

| Piece | Change |
|---|---|
| `AstraStartupCapability.name` | `uint32` fourcc becomes `char[16]`; every `ASTRA_CAPABILITY_*` constant follows |
| `console_shell.c` | `/`-rooted paths and `shell_resolve` become assign-rooted |
| `vfs_ext4_backend.c` | mount-point prefixing becomes a bound mount handle |
| VFS client Kit | assign table, resolution, and the rule that `..` stops at a root |
| The mounter | nothing enumerates volumes; the supervisor finds one partition and stops (§1.6) |
| `ASTRA_SYSCALL_LOG_WRITE` | appends to the trace ring; console becomes a sink |
| `AstraStartupInfo` | gains the launch context and the list of granted objects |
| NVRAM TLVs | state-volume identity; failsafe flag; reserved boot-attempt counter |

## 10. Open questions for the next specs

- The event record, levels and correlation are specified in
  `2026-08-06-event-system-design.md`.
- The shell language, and what a startup entry looks like once one exists.
- The bundle manifest: how an application declares the authority it wants,
  which documents it opens, and how a person sees and answers that request.
- System update: `SYS:` is a binding to a volume identity, so A/B images are
  two volumes and a rebind. Nothing here precludes it; nothing here specifies
  it.
- The mounter service: what it does when media arrives, what a person is shown,
  and what the command that binds a volume by hand looks like. §1.6 fixes the
  naming rule and leaves the service to its own spec.
Union assigns are **not** an open question. §1.7 decided against building them
and named the trigger that would reopen it.
