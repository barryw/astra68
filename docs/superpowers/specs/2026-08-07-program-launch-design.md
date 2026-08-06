# Astra program loading and launch

Date: 2026-08-07
Status: design. Nothing here is built. The kernel's half — ELF acceptance,
loading, mapping, the startup block, exit status and death waiters — has been
built and tested since the first boot; what is missing is a door to it and a way
for a launched program to reach a service.

Depends on `2026-08-06-filesystem-layout-design.md` (§2.5 command lookup, §3.2
the authority manifest, §4 applications), `2026-08-06-status-and-exit-design.md`
(what an exit means) and `docs/DRIVER_AND_SERVICE_ARCHITECTURE.md` §9 (the
service seam this finally moves).

## Why

Everything on this machine is one process. The storage service, the events
service and the shell are function calls inside the supervisor, and the reason
is not design — no syscall creates a process, so there is nowhere else for them
to be. Three consequences have accumulated:

- **A person cannot run a program.** The machine has a filesystem, a namespace
  and a terminal, and nothing a person puts on the volume can be executed.
- **Every service is a deployment lie.** Each one carries a comment saying it
  moves to its own process when a loader exists, and each of those comments is
  now load-bearing: `vfs_host.c`, `events_host.c`, the Kit's transport callback,
  the router's slot handles, the activity that crosses a boundary on the wire
  but never crosses one.
- **`events` is a shell builtin** because `COMMANDS:` cannot hold it.

This spec is the loader, the launch, and what a launched program is handed.

---

## 1. What a launch is

### 1.1 A launch creates no authority

A parent may hand a child only what the parent already holds, and never with
more rights than it holds. That single rule is what makes launching safe to
expose as a syscall: no path through it produces a capability that did not exist
a moment earlier, so the question "how did that program get to the disk" always
has an answer, and the answer is always a line somebody wrote.

It also fixes the shape of the call. There is no `fork`: nothing is inherited
implicitly, because implicit inheritance is exactly the mechanism that makes the
answer above unavailable on a Unix. There is no `exec` replacing the caller
either — a program that wants to become another program is asking for a
lifetime question nobody needs answered here.

**One call: create a process from an image and a list of grants.** Everything
else is layering.

### 1.2 The kernel already loads

`kernel_process_create_executable(image, size, capabilities, count, &id)` exists,
is tested, enforces the ELF acceptance profile in `sw/kernel/elf.c`, publishes
the startup block, seeds the capability table and makes the first thread
runnable — all or nothing, with every allocation rolled back on failure. The
firmware's initial image goes through it on every boot.

What is missing is a syscall that reaches it, and the rule in §1.1 applied to
the capability list a caller supplies rather than one the kernel invented.

### 1.3 The image exists twice during a launch

A caller reads an ELF out of a file into memory the caller owns, and the kernel
copies from there into the frames it maps. For the duration of the call the
image is in RAM twice.

That is worth stating rather than discovering: it means a **bounded load
buffer** with a written ceiling, the way `ASTRA_USER_IMAGE_MAX_SIZE` had to be
written down for the firmware's own image. The first ceiling is one launch at a
time and one buffer in the launching program.

The alternative — the kernel reading the image itself through a file handle — is
better and is not first. It means the kernel speaking the storage protocol,
which is a service dependency inside the kernel, and that is a decision that
should be made when there is a reason rather than to save a buffer.

### 1.4 Three copies, and how each one goes away

Task 1's build made the count exact. A launch copies the image three times, and
they are three different problems that happen to look like one:

1. **Disk into the launcher's buffer.** The kernel has no filesystem. Storage is
   a userspace service reached over the storage protocol, so there is no
   `kernel_open`, and the bytes have to be read by somebody in userspace before
   any syscall can be made about them.
2. **The launcher's buffer into a kernel window.** Two reasons, and the second
   is the one that would survive fixing the first. The headers are validated and
   then used, so reading them where the launcher can still write them is a
   double fetch: another thread in that process rewrites the program header
   table between the check and the load. `kernel_elf_accept_windowed` closes
   that by bringing the headers across once, into memory the launcher cannot
   reach. Separately, the source pages are in the launcher's map and the
   destination frames belong to a process that does not exist yet.
3. **The window into the child's frames.** One page of kernel memory, reused per
   segment page. This is the copy that is bounded rather than proportional to
   the program, which is why it is the one that costs the least to keep.

**The real fix is a page cache and file-backed mapping**, and it removes the
argument for all three rather than optimising any of them. A read-only text
segment is the same bytes for every instance of a program, so the end state is
that the child's `.text` *is* the cached frames: one read from the volume, ever,
shared by every process running that image, and a second `events` costing no
text at all. That is also what makes a shared library possible later, and what
makes a 16 MiB machine able to run several programs at once instead of several
copies of the same one. It is a real subsystem — eviction, coherency with a
write to a file that is currently mapped — and it is not milestone 1.

**The cheap middle step, if the copy ever shows up in a measurement:** take an
**area handle and an offset** instead of a raw pointer, and *transfer* it rather
than clone it. The launcher loses the mapping at the moment of the call, so the
double fetch is gone by construction rather than by copying, and the kernel can
work frame to frame with no bounce page. Areas already carry a retain; this
costs an ABI change to §2 and nothing else. It is strictly less than the page
cache and buys strictly less: copy 1 remains, and nothing is shared between two
instances of the same program.

Neither is scheduled. Copy 3 is bounded, so what it costs is one page and some
cycles — not correctness, and not a ceiling anybody will hit. **Trigger:** the
first measurement where launch latency matters, or the second concurrent
instance of one program.

---

## 2. The syscall

```
ASTRA_SYSCALL_PROCESS_CREATE
  data[1] = image address, in the caller's memory
  data[2] = image length
  data[3] = address of an AstraLaunchGrant array
  data[4] = grant count
returns
  data[1] = a process handle, in the caller's table
  data[2] = the new process id, generation-tagged
```

```c
typedef struct AstraLaunchGrant {
    char     name[ASTRA_CAPABILITY_NAME_MAX]; /* what the child will call it */
    uint32_t handle;                          /* the caller's own handle */
    uint32_t rights;                          /* a subset of what it holds */
    uint32_t root_offset;                     /* into the grant block's tail */
    uint32_t flags;
} AstraLaunchGrant;
```

Four rules, each of which is a refusal rather than a clamp:

- **A handle the caller does not hold is refused**, not skipped. A launch that
  silently dropped a grant would produce a child whose namespace is smaller than
  the line that launched it says, and the first symptom would be a path that
  does not resolve for a reason nobody can see.
- **Rights are a subset or the call fails.** Narrowing is the point: a shell
  holding `WORK:rw` may hand a child `WORK:r`, and cannot hand it `SYS:w`.
- **All or nothing.** A failure anywhere leaves no process, no frames, no
  mappings and no handles — which is what `kernel_process_create_executable`
  already guarantees and this must not weaken.
- **The returned process handle carries `QUERY | WAIT | TERMINATE`**, so the
  launcher can watch it, wait for it and kill it, and nothing else. `DEBUG` is
  not implied by having launched something.

**Arguments** travel in the same call: the startup block already carries `argc`
and `argv_address`, and validation already refuses a count with no vector. A
bounded copy of the words into the child's startup page is the whole mechanism.

**Naming the image.** The process-start event from the events spec §1.3 —
*process 0x… is `COMMANDS:events`, build 0x…* — is emitted here, by the kernel,
because this is the only place that knows both halves. It is what makes an
event's process id resolvable to a name by a reader that holds the authority.

---

## 3. A service becomes a process

This is the half that makes a launched program useful, and it is larger than the
syscall.

### 3.1 The transport swaps; no caller changes

`AstraVfsTransport` is a function pointer for exactly this. Today it is
`astra_vfs_local_transport`, a direct call into the service core in the same
process. It becomes a port send and a port receive.

The records already fit: an `AstraVfsRequestMessage` is 224 bytes plus the
header and an `AstraVfsReplyMessage` is 232, both under
`ASTRA_MESSAGE_INLINE_MAX`, and there is a static assertion in
`astra/vfs_service.h` that fails the build rather than the boot if that stops
being true. Ports carry up to `KERNEL_HANDLE_TRANSFER_MAX` handles per message,
which is how a reply hands back something the caller did not have.

### 3.2 A service is reached by a handle, never by a name

There is no registrar (layout §3.3). A client reaches a service because a port
send handle was placed in its capability table at launch. Nothing looks a
service up at runtime, so nothing can be impersonated by registering first.

### 3.3 An assign's handle becomes a port handle

`AstraAssign` already carries `(name, root, handle, rights)`. Today `handle` is a
session number, and the router in `vfs_host.c` matches it to one of two clients
with a slot in the high halfword — marked `ponytail:` when it was written,
because this is what replaces it. After this, an assign's handle *is* the port
send handle for the service that owns that mount, and the router disappears
rather than being improved.

### 3.4 A dead service is reported, not restarted

The kernel already models the honest answer as `PEER_DEAD`, and layout §3.3
already decided that a service that dies is reported rather than restarted:
its clients hold handles to it, and restarting silently would hand them a
service that does not remember them.

### 3.5 Activity crosses here

`AstraVfsRequest.activity` is on the wire today and adoption is a no-op, because
the service runs on the caller's own thread. `astra_activity_adopt` exists and
is unused. This is the boundary it was written for: a service adopts the
caller's activity for the duration of handling, and one request stays one story
across two processes.

---

## 4. What a child is handed

A launched program's namespace is its capability table, seeded from the grants,
and nothing else. No environment, no inherited working directory, no ambient
anything.

| Grant | What it means to the child |
|---|---|
| `WORK` → port handle, root `work`, `r`/`rw` | `WORK:` resolves; the rights are the ceiling |
| `COMMANDS` → the same service, root `commands`, `r` | it can read what it was launched from |
| `EVENTS` → the events service's port, `r` | it can read history |
| *(absent)* | the name does not resolve, and says so |

A parent that wants a child to see less says less. That is the whole mechanism,
and it is the same table the startup block already publishes.

### 4.1 Streams are capabilities, not numbers

A launched program needs somewhere to write and somewhere to read. Three more
grants, named the way everything else here is named:

| Grant | What it is | What it carries |
|---|---|---|
| `STDOUT` | a send handle to a text sink | bounded text, no reply |
| `STDERR` | a send handle to a text sink | the same, granted separately |
| `STDIN` | a send handle to a text source | a bounded request, and a reply with what there is |

**Not file descriptors 0, 1 and 2.** An integer table with a dup and a close is
the POSIX personality's job; here a stream is a capability with a name, so a
program that was not granted `STDIN` does not have one and says so, rather than
reading from whatever inherited the number.

- **Writing is fire and forget with back pressure.** A write is one bounded
  message and there is no reply, because a reply per line doubles the round
  trips at 30 MHz and nothing a program does depends on the sink's opinion. A
  full sink answers `WOULD_BLOCK`, which is the back pressure and the only
  answer a writer needs.
- **Reading is a request and a reply.** The child asks for at most N bytes and
  gets what there is, possibly short, possibly none — the same short-read rule
  the storage protocol already has, for the same reason.
- **Redirection is a different grant.** `events > log.txt` is the launcher
  handing a sink that writes to a file instead of the terminal, which is what
  makes redirection a capability operation rather than a shell trick. It is not
  built here; it costs nothing to leave the door open, and everything to close
  it by making streams integers.
- **The terminal service owns the terminal.** `TERMINAL_AND_POSIX.md` §2 already
  has the layering: the terminal owns the cell model and the shell does not draw
  through it. Today the supervisor hosts it, like every other service, and moves
  out on the same day they do.

**`STDERR` is not where a program says what went wrong.** It is where a program
says something a person should read *now*. What went wrong is an event, with a
level, an activity and a source location, and it is in `EVENTS:` whether anybody
was watching or not. A program that only writes its failures to `stderr` has
told the person at the terminal and told the machine nothing.

---

## 5. Command lookup, and what a directory in `COMMANDS:` means

Layout §2.5 fixes two places in one order: `APPS:`, then `COMMANDS:`. This spec
answers the question that arrives the first time there are more than a hundred
programs: **may `COMMANDS:` have subdirectories, and are they searched?**

### 5.1 Subdirectories exist. They are never searched.

A bare name resolves against the **top level of `COMMANDS:` only**. A program in
a category is named by its category: `dev/objdump`, `text/wc`.

### 5.2 Why not search them

Searching subdirectories is a search path with the configuration file removed
and the ordering hidden in the directory layout, which is worse than `PATH` in
every way that matters:

- **"Which one ran?" stops being answerable.** Two `wc`s in two categories, and
  the winner is decided by enumeration order — which is the filesystem's
  business, not a person's.
- **It is a hijacking surface.** Anything that can write one directory under
  `COMMANDS:` can shadow a command it does not own. Layout's whole §7 exists to
  stop a person breaking the machine by editing a file; a searched tree makes
  installing a file into a subdirectory a way to replace `ls`.
- **It costs a scan per invocation.** A thousand entries across categories is a
  thousand protocol round trips at 30 MHz, on every command, or a cache — and a
  cache is an index, which is §5.3.

The layout spec already refused a configurable search path for exactly these
reasons. A recursive one is the same refusal wearing a directory tree.

### 5.3 What else was considered

| Idea | Why not |
|---|---|
| An index file mapping name → path, written at install | A second source of truth that goes stale, and a file whose correctness the machine's behaviour depends on. Layout's governing rule refuses that. |
| Links from a flat top level into categories | There are no symbolic links (layout §1.8), and this spec is not the place to reopen that. |
| A directory in `COMMANDS:` is a bundle, like `APPS:` | Then a directory means two things and telling them apart means opening them. `APPS:` is where bundles live; keeping the two kinds in two places is what makes each rule one sentence. |
| Flat, with no subdirectories at all | Works — `/usr/bin` has a thousand entries and Unix survives — and it makes the thing a person browses the same thing the machine looks up. Rejected only because a category costs nothing once a name is not searched for. |

### 5.4 What this costs, stated

A program in a category is typed with its category, every time. `text/wc` is its
name, not a shortcut for `wc`. Two consequences, both accepted:

- Whoever ships or installs a program **chooses** whether it is common enough to
  live at the top level. That is curation, and curation is a person's judgement
  rather than a mechanism — which is the correct place for it.
- Completion, when the shell grows it, may search categories, because that is a
  human-time operation happening once at a keystroke rather than a machine-time
  one happening on every invocation. Searching to *offer* a name is not
  searching to *resolve* one.

### 5.5 A command is a file; an application is a directory

`COMMANDS:` holds single-file programs and directories of them.
`APPS:` holds bundles, which are directories with a manifest (layout §4.1). A
name that resolves to a directory in `COMMANDS:` is a category and is not
runnable; a name that resolves to a directory in `APPS:` is a bundle and the
manifest says what to run. Two places, two meanings, neither ambiguous.

---

## 6. What a program is

- **A single ELF**, accepted by the profile the kernel already enforces.
- **Entry is `crt0` → `astra_main(startup)`**, which is what the supervisor is
  built as today. A launched program and the initial image are the same shape.
- **The runtime is the library.** `sw/userspace/runtime` provides the
  freestanding subset — `memcpy`, the `str*` family, `qsort`, `assert`, the
  syscall wrappers, the event macros — and `sw/userspace/alloc` provides a
  bounded arena allocator. **No hosted C library is needed to run a program on
  this machine**, and one is not on the path to this.
- **A hosted libc is the POSIX personality**, which owns `/` and maps it onto
  granted assigns. It is for *ported* software, it has its own spec, and it is
  not a prerequisite for anything here.
- **A program may carry its own event catalog** (events spec §3), which is what
  makes its events readable by a reader that has never seen its source.

---

## 7. Exit, waiting, and what the launcher reports

The vocabulary exists and does not change. A child exits with a status; the
verdict bit is the system's alone and a program cannot forge one; a crashed
process cannot report success (`ASTRA_STATUS_FAULTED`, already enforced in
`retire_current`).

The launcher waits on the process handle it was returned, reads the status, and
reports it the way the shell already reports one. **The exit is an event**, so a
command that failed is in `EVENTS:` with the activity of the line that ran it —
which is the whole point of the activity, arriving at the case it was designed
for.

---

## 8. Accepted limits

- **One launch at a time**, and one bounded load buffer. Concurrency here buys
  nothing until there is something to run concurrently.
- **No `fork`, no `exec`, no signals.** Termination is a capability operation on
  a process handle, which is what `kill` becomes (`OBSERVABILITY.md` already
  settled that).
- **No environment.** Arguments are the interface. An environment is ambient
  state inherited implicitly, which is the thing §1.1 exists to refuse.
- **Static linking first.** `LIBS:` is in the layout for shared kits and a
  dynamic loader is a spec of its own; a program that links the runtime
  statically is a program that runs.
- **The image ceiling is stated, not discovered** — §1.3.

---

## 9. What this changes in existing code

| Piece | Change |
|---|---|
| `sw/include/astra/syscall.h` | `ASTRA_SYSCALL_PROCESS_CREATE`, and `AstraLaunchGrant` |
| `sw/kernel/process.c` | the handler: validate the grants against the caller's own table, then the existing loader |
| `sw/kernel/elf.c` | nothing; the profile is already what a launched program must satisfy |
| `sw/userspace/vfs/src/vfs_local_transport.c` | joined by a port transport; the local one stays for host tests |
| `sw/userspace/supervisor/src/vfs_host.c` | the service moves out; the router (§3.3) disappears |
| `sw/userspace/supervisor/src/events_host.c` | the same, and the drain moves with it |
| `sw/userspace/vfs/src/vfs_assign.c` | an assign's handle is a port send handle |
| `sw/userspace/supervisor/src/console_shell.c` | `events` stops being a builtin; a bare name becomes a launch |
| `sw/userspace/runtime` | `astra_launch()`, and `astra_activity_adopt` finally has a caller |

---

## 10. Open questions

- **Where the load buffer lives.** The launcher's own BSS is simplest and puts a
  fixed ceiling on program size; a mapped area is better and needs the area
  syscalls this machine already has. Decide with a number, not a preference.
- **Whether the kernel should read the image itself**, through a handle, once
  services are processes. It removes the double copy and adds a service
  dependency to the kernel. Not now, and worth revisiting when a program is big
  enough for the copy to matter.
- **What the shell does while a child runs.** Waiting is simplest and is what a
  terminal does; a background launch needs a job model, which is the shell
  language's business.
- **Whether a command may be granted `DEBUG`.** `events` reads the stream, which
  today needs it. If a command can hold it, the answer to "who may read the
  machine's account of itself" becomes a line in whatever installs commands —
  which is the right place, and it needs saying out loud.
