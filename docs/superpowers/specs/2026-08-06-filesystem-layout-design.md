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
| `APPS:` | installed applications, self-contained | a bundle is a directory |
| `EVENTS:` | the event store | bounded; see §5 |
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
  config/  apps/  events/  work/  temp/
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

## 4. Configuration

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

## 5. Where events live

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

## 6. Not breaking the machine

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

## 7. Accepted limits

- **Nothing can scan the whole machine.** No `find /`, no indexer, no
  search-everywhere without being handed everywhere. Correct for this machine,
  occasionally annoying.
- **Ported software needs the POSIX personality**, which owns `/` and maps it
  onto granted assigns. Planned rather than improvised.
- **Multi-user is not foreclosed.** `WORK:` is a binding, so a second person is
  a second binding rather than a layout change. The door costs nothing to leave
  open and nothing to leave shut.

---

## 8. What this changes in existing code

| Piece | Change |
|---|---|
| `AstraStartupCapability.name` | `uint32` fourcc becomes `char[16]`; every `ASTRA_CAPABILITY_*` constant follows |
| `console_shell.c` | `/`-rooted paths and `shell_resolve` become assign-rooted |
| `vfs_ext4_backend.c` | mount-point prefixing becomes a bound mount handle |
| VFS client Kit | assign table, resolution, and the rule that `..` stops at a root |
| `ASTRA_SYSCALL_LOG_WRITE` | appends to the trace ring; console becomes a sink |
| NVRAM TLVs | state-volume identity; failsafe flag; reserved boot-attempt counter |

## 9. Open questions for the next specs

- The event record: fields, levels, and what context the system attaches
  without being asked.
- The shell language, and what a startup entry looks like once one exists.
- How an application bundle declares the authority it wants, and how a person
  sees and answers that request.
- System update: `SYS:` is a binding to a volume identity, so A/B images are
  two volumes and a rebind. Nothing here precludes it; nothing here specifies
  it.
