# The Astra POSIX layer

Astra has capabilities, assigns and a VFS spoken over ports. Unix software
assumes file descriptors, a current directory and a root. This library is the
translation, and it is deliberately the *only* place that translation happens:
the kernel does not grow a Unix personality, and no NDK caller pays for one.

## What it is not

It is not a libc. The C library is [picolibc](https://github.com/picolibc/picolibc)
— built out of tree by `mk/build-picolibc.sh`, BSD-3/MIT-compatible, with all
non-BSD-compatible source removed upstream. picolibc supplies `printf`,
`malloc`, `qsort`, `strtol`, `regcomp`, `setjmp` and the rest of ISO C. What it
does not supply is the handful of functions that only a kernel can answer.
Those are this library.

## The rule

**A descriptor is an index into a table this process owns, and every entry
holds a capability the process was already granted.** Nothing here invents
authority. A program that was not handed `STDOUT` has no fd 1, and that is the
correct answer rather than a missing feature.

## What is implemented

| file | provides |
|---|---|
| `src/console.c` | the descriptor table. 0/1/2 arrive as the `STDIN`/`STDOUT`/`STDERR` stream capabilities; anything above them is what `open` put there. `write`, `read`, `close`, `isatty`, `lseek`, `_exit` |
| `src/file.c` | `open`, `stat`, `fstat`, `access`, `unlink`, `rmdir`, `mkdir`, `chdir`, `getcwd`, `opendir`, `readdir`, `closedir` |
| `src/heap.c` | `sbrk`, so picolibc's `malloc`, `free` and `realloc` work |

### One table, and why files are reached through a vector

A descriptor has to be able to change what it is: a shell redirecting output
makes fd 1 a file while stdio goes on writing to fd 1, so streams and files
cannot live in separate tables. But `hello` is 23 KiB and `ls` is 43 KiB, and
the difference is the filesystem — a program that only prints must not link it.
So the table holds a kind and a number, and the file operations arrive as a
vector that `src/file.c` registers the first time something calls `open`.

### A current directory is an assign plus a path

There is no root. `getcwd` answers `CWD:` or `CWD:proto`, in the machine's own
terms, because a slash-rooted string would name nothing and could not be handed
back to `open`. The starting point comes from the launcher: the shell grants
`CWD:` rooted where the prompt is standing, so `fopen("notes.txt")` opens the
file beside you. A program whose launcher said nothing starts at `WORK:`.

### The heap is an area

`malloc` is user space here for the same reason it is user space everywhere: a
syscall per allocation would be absurd, and a kernel has no business knowing
about a 24-byte struct. The kernel's memory management is **areas** — page
granular, quota'd, already built. picolibc's allocator carves one up.

`sbrk` is the sixty lines between them, and it creates the area **lazily**, on
the first allocation. The alternative — `__heap_start`/`__heap_end` in the
linker script, which picolibc's fallback `sbrk` uses — puts the heap in the
image's writable segment, where the loader maps every page of it at launch: a
megabyte of heap would be a megabyte of frames committed by `status`, which
allocates nothing. Pay for what you ask for.

The area is **reserved**, not committed: creating it names 2 MiB of address
space and spends no frames, and each page arrives on the fault that first
touches it, a cluster at a time, charged to this process then. So the heap
reserves the whole of what one VM slot will give rather than guessing, and a
program that allocates a megabyte and writes a page pays for a page.

There is no `astra_heap_bytes` any more. A knob whose only correct setting is
"as much as I turn out to need" is not a knob, and the ceiling that finally
refuses is the owner's frame quota, which the kernel already tracks. Shrinking
the break decommits the pages it passes, so releasing memory returns frames
instead of merely moving a pointer. One area is one 2 MiB VM slot, so that is
still the most a heap can be until `sbrk` learns to hand out a second,
discontiguous one — and until `malloc` has been checked against one.

**Link-order trap.** picolibc ships a *weak* `sbrk` in its own archive member.
Inside `--start-group` the linker pulls that member the moment `malloc` names
`sbrk`, the weak definition satisfies it, and the group never comes back for
the strong one here — which fails as `undefined reference to __heap_start` and
reads like a missing linker script. `console.c` keeps a used reference to
`sbrk` so the right one is already in the link.

## What is not, and what needs it

Ordered by what the first ported program will ask for. Nothing here is hard;
what matters is that each one has an Astra answer before it has a Unix one.

| missing | Astra answer | first needed by |
|---|---|---|
| `rename` | the protocol has no operation for it | any editor's save-by-rename |
| `realpath` | normalising is `path_normalise`; there is no root to resolve against | rarely |
| `dup2` `pipe` | a descriptor is a table entry, so `dup2` is a copy; a pipe is a stream port pair, which `console_stream.c` is already shaped for | **shell redirection**, then pipelines |
| `tcgetattr` `tcsetattr` `ioctl(TIOCGWINSZ)` | the terminal service | **vim** — raw mode is the first thing it asks for |
| `sigaction` `select` `poll` | events and ports already do the waiting | **vim** — `SIGWINCH`, input polling |
| `fork` `execve` `waitpid` | `astra_launch`, which is `posix_spawn` in shape rather than `fork` | vim's `:!` |
| `mmap` | areas | large-file editing; stub-able for a long time |

Two limits worth knowing rather than discovering:

- **`readdir` costs a round trip per two names.** The batch is two entries deep
  because that is what fits in picolibc's `DIR`; a trip is about 7.5 ms. Astra's
  own `ls` uses the native API and reads eight at a time.
- **`O_EXCL` is not atomic.** The protocol has no exclusive-create flag, so it
  is a `stat` and then an `open`, and another process could create the name
  between them.

`ps` is not on this list on purpose. It is not a POSIX problem: `PROCESS_INFO`
is handle-scoped so that a process cannot enumerate what it was not given, and
the answer is `PROC:`, the synthetic tree specified in `docs/OBSERVABILITY.md`
beside the `EVENTS:` tree that already ships. `ps` is `ls PROC:`.

## Building

picolibc is not vendored. `mk/build-picolibc.sh` clones upstream and builds it
with `mk/picolibc-cross-m68k-astra.txt`; git holds the generator, not the
product. Point `PICOLIBC` at the install prefix if it is not `~/picolibc-astra`.

## The gate

`COMMANDS:hello` proves stdio reaches a stream capability: it formats with
`snprintf`, checks the result byte by byte, prints with `printf`, and returns
non-zero if any of it is wrong.

`COMMANDS:posix` proves the rest. It writes a file, reads it back, seeks into
it, stats it by descriptor and by name, lists the directory it is in, walks into
that directory and opens the same file by a bare name, removes everything it
made, and allocates. Each check has its own exit code, so a failure names the
step. Both run in `emu/qemu/test-terminal.py`.

The verdict is the exit status rather than the text, because the text lands on a
screen and the status lands in the trace ring.
