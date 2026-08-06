# Astra namespace and filesystem layout

Date: 2026-08-06
Status: design, approved in conversation; not implemented

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

### 1.6 No symbolic links

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
| `EVENTS:` | the event store | bounded; see §6 |
| `WORK:` | the person's files | the only place they live |
| `TEMP:` | scratch | emptied at every boot |

### 2.3 Synthetic

`PROC:`, as specified in `OBSERVABILITY.md`.

### 2.4 The tree

```
SYS:                      read-only mount
  commands/  libs/  drivers/  services/  fonts/
  defaults/               -> DEFAULTS:
  startup/                -> STARTUP:
  version                 what this system image is

<state volume>            writable mount
  config/  events/  work/  temp/
  apps/
    Editor/               one directory per application
      manifest  program  libs/  resources/
```

### 2.5 Command lookup

A bare command name is searched in exactly two places, in this order:

1. `APPS:`
2. `COMMANDS:`

`APPS:` first, so a person can replace a shipped command deliberately. Two
entries, one stated order, no configurable search path: a path variable is a
hijacking surface and an unanswerable "which one ran" question, and neither is
worth the flexibility on a machine with two locations.

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
service SERVICES:events   grants EVENTS:rw TEMP:rw          required
service SERVICES:storage  grants DRIVERS:r  CONFIG:r        required
service SERVICES:input    grants CONFIG:r                   required
command COMMANDS:shell    grants WORK:rw APPS:r COMMANDS:r LIBS:r  required
```

One file states what every program on the machine may touch. That is not
answerable on a Unix at any price, and it is answerable here because the
namespace is granted rather than ambient.

**A grant whose binding does not exist is omitted, not fatal.** With no state
volume there is no `APPS:` to hand the shell, so the shell starts with a
smaller namespace and naming `APPS:` fails cleanly inside it. An entry fails
only when its own program fails. Without this rule §3.4 would be a promise the
manifest could not keep.

### 3.3 The event sink starts first

It is what records the failures of everything after it. Before it exists,
failures go to the console channel, which needs no service and no disk.

### 3.4 A machine with no state volume still boots

Fresh disk, failed disk, first power-on: `SYS:` alone yields a working system.
`EVENTS:` and `TEMP:` fall back to RAM, `CONFIG:` is absent and defaults apply,
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

**`EVENTS:` is a bounded on-disk ring.** It has a size budget, it drops oldest,
and it never grows without limit. A machine that will not boot because it
logged too much about not booting is `/var/log` in a nicer hat.

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
