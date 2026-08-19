# Astra 68 — Handover: the boot that was broken, and a shell you can read

Date: 2026-08-19. Written to be read cold. Read `CLAUDE.md` first, then
`docs/HANDOVER-libc-and-limits.md`, which this continues: its §10.1 and §10.2
are done — the red kernel test and every host-QEMU gate — and everything from
its §10.3 down is untouched and still the plan.

**Where it stands. Every gate is green.** Kernel 30 of 30, `sw/userspace`,
`test-display.py`, `time-boot.py`, `test-events.py`, `irq_quarantine_probe.py`,
and the terminal gate — rewritten against a channel that exists, driving the
desktop, opening a terminal by double click, and reading what the shell said out
of the trace ring — at `ASTRA TERMINAL PASS 33 commands`. `cat`, `mkdir` and
`rm` are programs in `COMMANDS:` rather than shell builtins (§10), and the POSIX
layer has files, directories, a current directory and a heap (§11). Nothing is
committed.

---

## 1. The tree did not boot at all

Not the terminal gate. The kernel panicked before userspace ran:

```
Initial image ....... rejected, status 9
Reason: initial user image rejected
```

Status 9 is `KERNEL_PROCESS_CORRUPT`, from `grant_bootstrap_capabilities` →
`kernel_process_grant_device` → `process_pool_valid()` →
`kernel_area_pool_valid()`. The area whose check failed had
`generation == 0x5AA55A5C` — a POST memory-test pattern.

**`.tables` is `NOLOAD` and nothing zeroed it.** `entry.S` cleared
`_bss_start.._bss_end` only. When the object tables moved out of BSS into their
own region they kept being written against BSS's guarantee: `kernel_area_pool_init`
preserves a nonzero generation on purpose, so a pattern-filled one survives
initialisation and fails `generation > AREA_GENERATION_MASK` on the first
validity check the machine makes.

Fixed in `sw/kernel/entry.S`: the same 16-byte burst loop, over
`_kernel_tables_start.._kernel_tables_end`, right after the BSS one. Both bounds
are `ALIGN(16)` in `kernel.ld`, so the burst is exact.

**How it was missed.** The last kernel build that booted was the ROM on the
board, built 12:50; the last kernel edits went in after it and were only ever
checked against host tests, which do not run `entry.S`. The kernel image is
129,900 bytes now and 129,400 in that ROM — a five-hundred-byte difference is
the whole tell, and it is worth checking whenever a boot fails in a way that
looks impossible:

```
strings <rom> | head -2                       # its build stamp
qemu-system-m68k -M astra68 -m 128M -bios <rom> \
  -display none -monitor none -serial stdio -no-reboot
```

The board still has the last known-good ROM at
`/data/astra/rom/astra_boot-cmds.bin`; it boots under host QEMU and was what
proved the regression was in the tree rather than in the harness.

## 2. The kernel gate

`test_real_handle_exhaustion_rolls_back_thread_create` fills the handle table
**deliberately** now: one area, then `HANDLE_DUPLICATE` until
`kernel_process_test_handle_count == KERNEL_HANDLE_MAX_ENTRIES`. That is the
idiom `test_area_publication_rolls_back_when_handle_table_full` already used.

The old version reached "full" as a by-product of every owner quota, and those
quotas no longer add up to a full table — syncs, ports, areas, rings and a
process's own threads together reach 110 of 128 — so it asked for a seventeenth
thread in a sixteen-thread process and failed for a reason the test is not
about. Duplicates also leave the thread pool untouched, which is the point:
`THREAD_CREATE` then fails on the handle install, and the install is the failure
whose rollback is under test.

## 3. A failed service now says which check it failed

`receive_ready` in `sw/userspace/supervisor/src/loader.c` waits two ways for a
child to publish ready. One path reaped the child and returned its exit status;
the other — the wait on the ready port — returned `PEER_DEAD` and threw the
status away. The child dying is the usual reason that wait ends, so the usual
outcome was the least informative one.

It reaps first now. The same boot failure that read

```
Initial image ....... EXITED, reason 1 status 0x00000010
```

reads `status 0x00000009` — `ASTRA_STATUS_BAD_HANDLE`, which is the terminal
saying it was not given a capability it needs. That one change is what turned
§4 from a hunt into a reading.

## 4. One startup profile, because the terminal is a window client

`emu/qemu/astra_image.py`'s `STARTUP_MANIFEST` still described the machine as it
was before the window runtime landed in `19bed05`: it granted `SERVICES:terminal`
`DISPLAY INPUT INPUT_IRQ` and started no display service. The terminal is a GUI
client now — it asks for `ASTRA_CAPABILITY_GUI` and returns `BAD_HANDLE` without
it — so that manifest started a program that refused before publishing ready,
every time, and the supervisor exited.

Four host-QEMU gates installed with that manifest: `test-terminal.py`,
`test-events.py`, `time-boot.py`, `irq_quarantine_probe.py`. All four were dead
on it, and had been since that commit.

`STARTUP_MANIFEST` **is** the desktop manifest now — one profile, and
`DISPLAY_STARTUP_MANIFEST` is the same object under its old name. Two things
forced that rather than "the same manifest plus a display service":

- an app bundle, not a service image. `SERVICES:terminal` launched directly has
  no `APP:` assign, so `load_title_icon` fails and it exits `TERMINAL_FAIL_ICON`.
  Only an `APPS:*.app` entry gets a bundle root (`resolve_entry_image`).
- **somebody has to hold `APP_LAUNCH`.** The supervisor closes its own sender on
  the launch port at the end of `supervisor_loader_start`, deliberately, so the
  port dies with its last client. With no `APP_LAUNCH` in anybody's grants that
  is immediately, and `supervisor_loader_watch` exits `PEER_DEAD` the moment the
  machine is up. The desktop is what holds it.

So a gate that wants a terminal opens one the way a person does: a double click
at `(70, 90)` on the desktop's Terminal icon. `/tmp/drive2.py` on `beast` has
done this on the board all along.

## 5. Reading a terminal that draws glyphs

Nothing writes `VEGA_POST_TEXT` any more. The gate used to read cells out of
that plane; the terminal composites a window now, and a screen made of pixels
says nothing to anything but an eye. `ASTRA_SYSCALL_CONSOLE_WRITE` still exists
but needs the `DISPLAY0` lease, and a device has one owner — the display
service — so the terminal cannot write there even if it wanted to.

**The terminal model echoes each completed line.** `AstraTerminalEcho`,
installed with `astra_terminal_set_echo`, alongside the renderer and independent
of it: what draws a terminal and what records it are two different jobs, and a
headless machine wants the second without the first. `console_shell.c` installs
one that calls `astra_log_debug`.

Three details that each cost a run:

- **A carriage return discards the line, it does not deliver it.** A line editor
  redraws after every keystroke; without this the ring got every prefix of every
  command. What reaches the echo is the last version, which is what the screen
  shows.
- **Debug level, not notice.** `astra_event_store_append` drops
  `ASTRA_EVENT_LEVEL_DEBUG` at drain — live subscription only, counted, never
  stored — while everything above it lands in a **256-record** store. At notice
  level the shell's own commentary evicted the machine's record of what
  happened: `cat events:activity/00000005` came back holding the command and not
  the refusal it answered with. `astra_log_debug` is new in
  `sw/userspace/runtime/src/log.c` and is `astra_log_write` with the level
  changed.
- The kernel refuses only levels above `ERROR`, so debug still reaches the ring.
  That asymmetry is the whole mechanism.

The gate reassembles the text: a record carries twenty bytes and the decoder
marks a continued one with a trailing backslash, so most of what is asserted
(`/commands/status [1]`, `namespace bound`) straddles two records and a check
against single records would fail on the length of its own needle.

## 6. What the rewritten gate does

`emu/qemu/test-terminal.py`, same name and same `SCRIPT`. It boots once and
types a command — the last line of `SCRIPT` reads the *previous* boot back, and
that is only observable from the boot after one — then boots again, waits for
`stage 8`, double-clicks Terminal, waits for the banner, and types each line,
waiting for its answer among the records **that line's Enter produced**. The
ring is cumulative and `exited 0` is true of something on almost every boot.

`ASTRA TERMINAL PASS 23 commands`.

Two things had to be waited for that the screen used to say:

- **the shell drops what is typed before it prints its prompt**, and the prompt
  carries no newline, so no record announces readiness. `Machine.settle()` waits
  for silence instead. Typing into the tail of a redraw loses the first
  characters and the shell then answers a question nobody asked, which reads
  exactly like a bug in the command.
- the boot before the run, above, is `warm_the_store()`.

**Two assertions changed, and each one is a finding:**

| was | is | why |
|---|---|---|
| `events` → `namespace bound` | `events` → `launching from place` | the bare command prints the last screen of the store, and the namespace record is the first thing the machine writes. `--all` does not help: it means all *levels*, not all history. The whole-boot read is the last line of `SCRIPT`, `events --boot -1`, which still asserts `namespace bound`. |
| `ls commands:` → `devices  [0]`, `status  [1]` | `ls commands:` → `devices`, `which` | **`ls` no longer says which member a name came from.** That annotation went when `ls` stopped being a shell builtin and became a program, and nothing replaced it. `which` still prints it, which is the only reason the ordering claim is tested at all. Worth putting back. |

`emu/qemu/test-events.py` and `emu/qemu/irq_quarantine_probe.py` now borrow
`open_terminal()`, `settle()`, `sequence()` and `wait_for_text()` from the gate
module rather than keeping their own copies — `test-events.py` lost about sixty
lines of duplicated QMP in the process. Both pass. The quarantine probe exits 0:
the whole script plus twenty-four launches, and storage stayed alive.

**What the rewrite dropped**, deliberately and worth knowing before you look for
it: the cursor-tracking checks, the shared-plane-versus-guest-memory comparison,
the `cached-ls` CPU-cycle budget, and the `--keyboard-evdev`, `--pointer-evdev`,
`--startup-soak`, `--release-io` and `--session-only` modes. All of them read or
drove the character plane. The command-latency budget (`--performance-only`)
survives, measured Enter-to-answer off the ring.

## 7. One product limit moved

`KERNEL_AREA_OWNER_MAX` 4 → **8**, and `KERNEL_VM_AREA_SLOT_COUNT` 16 → **32**
with it, so the quota stays a quarter of the pool and still means no one process
can spend everybody else's share.

Four was one short. A terminal in a window owns its surface **and** a transfer
area for each mount it reads through, so it ran out between `WORK:` and
`EVENTS:` — and what a person saw was `cat: limit reached` on a file that was
there. The area window is 64 MiB at `KERNEL_VM_AREA_BASE` now and the DMA window
starts at `0x50000000`, so it has room to double again before the two would
meet. `KERNEL_RING_OWNER_MAX` (16) still exceeds the area quota, which the ring
pool depends on.

## 8. Machines and commands

Everything below ran on `beast` in `~/astra68-verify`, an rsync copy of the Mac
tree. The Mac cannot build the kernel.

```
rsync -a --delete --exclude 'build*/' --exclude '*.o' \
      --exclude 'astra_kernel.*' --exclude 'astra_boot.*' --exclude 'astra68.rom' \
      sw/ beast:astra68-verify/sw/
rsync -a emu/ beast:astra68-verify/emu/

ssh beast 'cd ~/astra68-verify/sw/kernel    && make -j8 test'   # 30/30
ssh beast 'cd ~/astra68-verify/sw/userspace && make -j8 test'
ssh beast 'cd ~/astra68-verify/sw/userspace && make -j8'        # apps, for the volume
ssh beast 'cd ~/astra68-verify/sw/boot      && make -j8'        # ROM
```

Host QEMU:
`~/.cache/astra68/qemu-9.2.4/build-host-d5e02d.../qemu-system-m68k`.
Image `/tmp/storage-cmds.img`. Then:

```
for gate in test-display time-boot test-events test-terminal irq_quarantine_probe; do
  python3 emu/qemu/$gate.py $QEMU sw/boot/astra_boot.bin --image /tmp/storage-cmds.img
  echo "$gate=$?"
done
```

**Check the status, not the tail.** `... | tail` and then `$?` reports `tail`;
three gates looked green that were not. The whole sweep is about twenty minutes,
most of it the terminal gate's two boots.

**`sw/userspace/apps/build/Terminal.app` is what the volume gets, not
`sw/userspace/services/terminal/build/m68k/terminal`.** A change to the shell or
to the terminal model needs `make` in `sw/userspace` (not just `make test`)
before `astra_image.install` copies it, and a run against a stale bundle looks
exactly like the change having no effect. `apps/Makefile` builds the service
binary only when the file is **missing**, so the top-level order is what saves
you.

## 9. Traps this session paid for

- **`.tables` is not zeroed.** §1. Anything moved into `KERNEL_TABLES` must not
  assume BSS's zeros — that is now true because `entry.S` clears it, but the
  region is `NOLOAD` and the next region added the same way will not be.
- **A gate's exit status through a pipe is the pipe's.** `... | tail` and then
  `$?` reports `tail`. Three gates looked green that were not. Redirect to a
  file and check the status, or use `PIPESTATUS`.
- **The shell drops what is typed before it prints its prompt**, and the prompt
  carries no newline, so nothing announces readiness. The gate waits for
  silence instead (`Machine.settle`). Typing into the tail of a redraw loses the
  first characters and the shell then answers a question nobody asked — which
  reads exactly like a bug in the command.
- **The event store is 256 records and drops only debug.** Anything chatty at
  notice level or above is evicting somebody's history. §5.
- **Nothing had ever printed to stderr from a program**, so the fact that
  `write()` refused back pressure instead of waiting had never surfaced. §10.
  Treat "no code path has run this" as a reason to suspect it, not to trust it.
- Everything in `docs/HANDOVER-libc-and-limits.md` §9 still holds, in
  particular the dirty-ext4-journal one.

## 10. `cat`, `mkdir` and `rm` are programs

They were builtins. They are `COMMANDS:` images now, built like the others from
`<name>/<name>.c` in `sw/userspace/commands`. `ls` and `events` had already gone
that way; this finishes everything that can go.

**`write` stays a builtin, on purpose.** It exists because there is no other way
to put bytes in a file, and the answer to that is a stream pointed somewhere
other than the terminal — `cat` into a redirect, and eventually an editor — not
a better builtin. `console_stream.c` already anticipates it: stdout and stderr
are separate grants of the same handle *"so a launcher can later point them at
different things, which is what makes redirecting one of them a capability
operation rather than a shell trick."*

**`cd`, `pwd`, `assign`, `clear` and `help` cannot go.** They read or change the
shell's own state, and a child process changing its parent's directory is not a
thing.

### The grant that made it possible

A program has no current directory. The machine has no root either — a path is
`ASSIGN:path` and nothing else — so `cat foo` typed after `cd proto` names
nothing at all once `cat` stops being a builtin that qualified against
`shell.directory` before it touched anything. That would have been a silent
regression on the day of the move.

So the shell says where it is standing, the only way this machine says anything:
as a grant. `launch_grants()` adds **`CWD:`**, one per member of the assign the
prompt is in, with the directory folded into the root so the child names the
place and cannot walk above it. A program qualifies a bare name against `"CWD"`
and leaves an `ASSIGN:path` one alone — `astra_path_qualify` returns an absolute
path untouched, which is what makes one call handle both.

`ASTRA_LAUNCH_GRANT_MAX` 10 → **12** for it. Ten was exactly what the terminal
service's own launch needed, with nothing spare. It is ABI: the kernel's
`KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX` is defined as it, and
`KERNEL_PROCESS_HANDLE_DEMAND` counts two handles per grant.

**`ls` was already wrong about this** and nobody had noticed: a bare `ls`
listed `WORK:` unconditionally, so `cd proto` followed by `ls` quietly listed
the wrong directory. It lists `CWD:` now, falling back to `WORK:` for a
launcher that says nothing about where it is.

### Two things the move surfaced

- **`write()` answered `EAGAIN` to back pressure**, and stdio takes that as an
  error and stops. Buffered stdout never showed it — its writes are spaced far
  enough apart that the sink drains between them — but unbuffered stderr writes
  each segment of a format string back to back, so
  `fprintf(stderr, "rm: %s: %s\n", ...)` came out as **`rm: `** and nothing
  else. `sw/userspace/posix/src/console.c` now waits and yields, exactly as
  `read` in the same file already did and for the same stated reason: a stream
  with nothing ready is not a POSIX end of file, and a sink with no room is not
  a failed write. Nothing in the tree had ever printed to stderr from a program
  before.
- **The status words were a private table in the shell.** Four copies would have
  been four chances to call the same refusal two different things, so they are
  `astra_vfs_status_text()` in the VFS library now, declared beside the status
  codes themselves. The shell's `report_status` reads from it.

### What the gate covers

`ASTRA TERMINAL PASS 32 commands`. Nine of them are new and all about the grant
above: `cd proto`, `pwd`, `mkdir inner`, `ls`, `write scratch.txt hi`,
`cat scratch.txt`, `rm scratch.txt`, `cd`, `ls`. Bare names, resolved by a
program, in a directory the shell walked into.

`("mkdir proto", "proto")` became `("mkdir proto", "mkdir: exited 0")` — a
program says nothing when it works, so the old needle matched the line being
typed and would have passed whether the directory was made or not.

**Two `assert`s now sit under `SCRIPT`.** The `cat events:activity/N` lines name
an activity by number and the number is a position in the list; inserting a line
above either one sends the gate to somebody else's story. That is how those nine
lines broke them, and the assertions turn the next occurrence into a named
failure instead of "the wrong command is unrecorded".

## 11. The POSIX file half, and a heap

`sw/userspace/posix` was one file — descriptors 0, 1 and 2 over stream
capabilities. It is three now, and `COMMANDS:posix` runs every one of them on
the machine as part of the terminal gate: `ASTRA TERMINAL PASS 33 commands`,
with `posix: exited 0` as the last line.

| file | what |
|---|---|
| `src/console.c` | the descriptor table: `write`, `read`, `close`, `isatty`, `lseek`, `_exit` |
| `src/file.c` | `open`, `stat`, `fstat`, `access`, `unlink`, `rmdir`, `mkdir`, `chdir`, `getcwd`, `opendir`, `readdir`, `closedir` |
| `src/heap.c` | `sbrk`, so picolibc's `malloc`, `free` and `realloc` work |

### One table, because a descriptor changes what it is

Streams and files cannot live in separate tables: redirection makes fd 1 a file
while stdio goes on writing to fd 1. But `hello` is 23 KiB and `ls` is 43 KiB
and the difference is the filesystem, so a program that only prints must not
link it. The table therefore holds a kind and a number, and the file operations
arrive as a **vector `file.c` registers the first time something opens
anything**. `status` links neither half and is still 9,104 bytes.

### A current directory is an assign plus a path

`getcwd` answers `CWD:` or `CWD:proto`. Not a slash-rooted string: there is no
root, and what comes out of `getcwd` has to be something `open` would accept.
The starting point is the launcher's — the `CWD:` grant from §10 — so
`fopen("notes.txt")` opens the file beside you, and a program whose launcher
said nothing starts at `WORK:`.

### The heap is an area, created lazily

`malloc` is user space here for the reason it is user space everywhere: a
syscall per allocation would be absurd and a kernel has no business knowing
about a 24-byte struct. The kernel's memory management is **areas** — page
granular, quota'd, already built — and picolibc's allocator carves one up.
`sbrk` is the sixty lines between them.

It creates the area on the **first allocation**, not at startup. The obvious
alternative was `__heap_start`/`__heap_end` in `astra_user.ld`, which picolibc's
fallback `sbrk` already uses and which costs no code at all — and puts the heap
in the image's writable segment, where the loader maps every page at launch. A
megabyte of heap would then be a megabyte of frames committed by `status`, which
allocates nothing, on every launch.

The ceiling is `astra_heap_bytes`, weak, 256 KiB, and a program that needs more
defines its own. One area is one 2 MiB VM slot, so that is the most one heap can
be until `sbrk` can hand out a second, discontiguous one — and until a malloc
has been checked against that.

**The link-order trap this cost.** picolibc ships a *weak* `sbrk` in its own
archive member. Inside `--start-group` the linker pulls that member the moment
`malloc` names `sbrk`, the weak definition satisfies the reference, and the
group never comes back for the strong one. It fails as `undefined reference to
__heap_start`, which reads like a missing linker script rather than the wrong
allocator winning. `console.c` keeps a `used` reference to `sbrk` so the right
one is already in the link before picolibc is reached.

### One shared namespace, counted

`astra_process_filesystem_open` re-seeded file-scope state every call, so a
second holder tore the first one's VFS clients out mid-read. That never happened
while a command was the only thing asking — and it is exactly what happens the
moment `open()` is called in a command that also uses the Astra API directly.
It is reference counted now: seeded once, taken down by the last holder.

### What is deliberately not there

`rename` (no operation on the wire), `realpath` (nothing to resolve against),
and the vim set — `tcgetattr`, `sigaction`, `select`, `fork`. Two limits worth
knowing rather than discovering: **`readdir` costs a round trip per two names**,
because that is what fits in picolibc's `DIR` and a trip is 7.5 ms — Astra's own
`ls` reads eight at a time through the native API — and **`O_EXCL` is not
atomic**, being a `stat` and then an `open`.

## 12. Ranked next steps

1. **Redirection, then pipes.** §10 and §11 — `dup2` is a copy of a table
   entry, a pipe is a stream port pair, and `console_stream.c` already says
   pointing stdout somewhere else should be a capability operation rather than
   a shell trick. `write` stops being a builtin the day this lands.
2. **Terminal, signals and `select`** — the vim-critical set:
   `tcgetattr`/`tcsetattr`, `ioctl(TIOCGWINSZ)`, `sigaction`, `select`.
3. **Put the union member back in `ls`.** §6. A listing that does not say which
   member answered for a name is a union you cannot see, and `which` should not
   be the only thing that can tell you.
4. **Charge kernel objects to the owner ledger.** §7 here is the third resource
   this month resized by hand; §10.5 there is the destination.
5. **READDIR into the transfer area**, once a large listing measures badly.
