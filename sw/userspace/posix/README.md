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
| `src/console.c` | the descriptor table. 0/1/2 arrive as the `STDIN`/`STDOUT`/`STDERR` stream capabilities; anything above them is what `open` or `pipe` put there. `read`, `write`, `close`, `dup`, `dup2`, `fcntl`, `pipe`, `isatty`, `lseek`, `_exit` |
| `src/file.c` | `open`, `stat`, `fstat`, `access`, `unlink`, `rmdir`, `mkdir`, `chdir`, `getcwd`, `opendir`, `readdir`, `closedir` |
| `src/heap.c` | `sbrk`, so picolibc's `malloc`, `free` and `realloc` work |
| `src/startup.c` | standard `main(argc, argv, envp)` entry over Astra's native startup block |
| `src/terminal.c` | termios state, raw/canonical switching, window size, drain and flush |
| `src/poll.c` | `poll` and `select` over transferable stream readiness events and native wait-multiple |

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

### The heap is clone-private anonymous memory

`malloc` is user space here for the same reason it is user space everywhere: a
syscall per allocation would be absurd, and a kernel has no business knowing
about a 24-byte struct. The kernel reserves anonymous address space page by
page and charges physical frames only when touched. picolibc carves that up.

`sbrk` creates the reservation **lazily**, on
the first allocation. The alternative — `__heap_start`/`__heap_end` in the
linker script, which picolibc's fallback `sbrk` uses — puts the heap in the
image's writable segment, where the loader maps every page of it at launch: a
megabyte of heap would be a megabyte of frames committed by `status`, which
allocates nothing. Pay for what you ask for.

The heap reserves the complete anonymous window and spends no frames for the
reservation. Each data or allocator-metadata page arrives on first touch,
charged to this process. The address-map boundary is 504 MiB; installed RAM,
the owner quota, and the kernel reserve are the physical limits.

There is no `astra_heap_bytes` any more. A knob whose only correct setting is
"as much as I turn out to need" is not a knob, and the ceiling that finally
refuses is the owner's frame quota, which the kernel already tracks. Shrinking
the break decommits the pages it passes, so releasing memory returns frames
instead of merely moving a pointer. Page indices and free extents are 32-bit,
so the allocator introduces no smaller heap ceiling.

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
| `realpath` | normalising is `path_normalise`; there is no root to resolve against | rarely |
| `mmap` | areas | large-file editing; stub-able for a long time |

`execve` is an atomic replacement over the shared native ELF loader. POSIX
exports descriptor/VFS state into the opaque startup handoff, so redirections,
open files and duplicate descriptions survive while `FD_CLOEXEC` is honored.

Creation modes are complete through the shared stack: `open(O_CREAT, mode)`,
`mkdir(mode)`, `umask`, `chmod`, and `readlink` use VFS protocol 14 and
filesystem.library ABI 1.3. The mode is installed before a new inode is linked,
so there is no create-then-chmod visibility window.

One limit worth knowing rather than discovering:

- **`readdir` costs a round trip per two names.** The batch is two entries deep
  because that is what fits in picolibc's `DIR`; a trip is about 7.5 ms. Astra's
  own `ls` uses the native API and reads eight at a time.

`ps` is not on this list on purpose. It is not a POSIX problem: `PROCESS_INFO`
is handle-scoped so that a process cannot enumerate what it was not given, and
the answer is `PROC:`, the synthetic tree specified in `docs/OBSERVABILITY.md`
beside the `EVENTS:` tree that already ships. `ps` is `ls PROC:`.

## Building

picolibc is vendored in `third_party/picolibc`; `mk/build-picolibc.sh` builds it
out of tree with its Astra cross file. Git holds the source, not the generated
library. Point `PICOLIBC` at the install prefix if it is not
`~/picolibc-astra`.

`kit/astra-posix.mk` is the startup kit for external POSIX source trees. It
publishes the target flags, shared CRT0, linker script, libraries, and the
program-record source. Compile that source once with the six
`ASTRA_POSIX_PROGRAM_*` metadata definitions, then link it with
`$(ASTRA_POSIX_CRT0)`, the application's objects, and `$(ASTRA_POSIX_LIBS)`.
The same CRT0 starts native Astra programs through their own `astra_main`; the
POSIX archive supplies the bridge to ordinary `main(argc, argv, envp)` only
when a program needs it.

## The gate

`COMMANDS:hello` proves stdio reaches a stream capability: it formats with
`snprintf`, checks the result byte by byte, prints with `printf`, and returns
non-zero if any of it is wrong.

`COMMANDS:posix` enters through ordinary `main(argc, argv, envp)`, verifies a
Vim-shaped argument vector, then proves the rest. It exercises a real native
pipe including duplicated-writer lifetime and EOF, writes a file, reads it
back, seeks into it, stats it by descriptor and by name, lists the directory it
is in, walks into that directory and opens the same file by a bare name,
removes everything it made, and allocates. Each check has its own exit code,
so a failure names the step. Both run in `emu/qemu/test-terminal.py`.

The verdict is the exit status rather than the text, because the text lands on a
screen and the status lands in the trace ring.
