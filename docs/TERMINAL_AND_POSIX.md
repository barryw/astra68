# Astra terminal, POSIX personality, and zsh direction

Status: design direction; the terminal, PTY, POSIX personality, zsh, and Vim
described here are not implemented.

## 1. Goals

**LOCKED:** Astra is GUI-first, but the terminal is a first-class environment,
not a recovery afterthought. It must be quick to open, pleasant for sustained
work, scriptable, and capable of supporting serious editors and development
tools.

**DIRECTION:** Port upstream zsh as the primary interactive shell and Vim as an
early full-screen application. Astra should not reimplement a shell that merely
resembles zsh unless an evidence-backed port attempt proves the real shell
impractical.

The exact upstream revisions are pinned when port work starts. Astra-specific
patches remain small, documented, and exercised against upstream tests.

## 2. Stack and ownership

```text
terminal window/application
  cell model | scrollback | selection | rendering | clipboard
                       |
                       v
terminal/PTY service
  master/slave streams | termios | window size | foreground job
                       |
                       v
POSIX personality + libc
  fd table | pipes | paths | process groups | signals | wait
                       |
                       v
native Astra mechanisms and services
  handles | ports | shared areas | spawn | VFS | process death | waits
```

The terminal emulator does not own shell policy. The shell does not draw
pixels. The POSIX personality does not own native Astra process or resource
semantics.

## 3. Terminal behavior

**DIRECTION:** Define an `astra-256color` terminal description and ship its
terminfo entry. Do not claim `xterm-256color` compatibility until every claimed
sequence has tests.

The initial useful terminal supports:

- UTF-8 input and output with a deterministic cell-width policy;
- Astra Mono hardware glyph rendering;
- 16 basic colors plus a 256-entry indexed palette;
- cursor movement, style, visibility, save/restore, and erase operations;
- scroll regions, insert/delete line and character;
- alternate screen, bracketed paste, focus and resize events;
- bounded scrollback with explicit memory accounting;
- mouse reporting needed by terminal applications;
- copy, paste, selection, search, and link/path recognition in the GUI;
- clean handling of malformed or incomplete escape sequences.

Output is parsed in bounded chunks. Damage is accumulated by row and rendered
as batched glyph/background runs. Scrolling uses surface blits or a ring surface
rather than redrawing every cell with the CPU.

A terminal event loop waits simultaneously for PTY data, input, resize,
animation/cursor timers, render fences, process death, and closure. It must not
create helper threads to emulate wait-multiple.

## 4. PTY and stream model

A PTY pair consists of process-owned master and slave stream handles plus a
terminal-control object. Both directions have bounded byte rings and waitable
readable/writable state.

- A full output ring applies backpressure to that terminal session only.
- Closing either endpoint wakes every waiter with a defined terminal result.
- Window-size changes are ordered and observable by the foreground job.
- Input processing, echo, canonical/raw mode, and control characters follow the
  advertised termios contract.
- Scrollback belongs to the terminal application, not the PTY byte stream.
- PTY and pipe buffers are charged to the creating process or service.

Exact ring sizes and line-discipline placement remain **OPEN** until measured.

## 5. Native versus POSIX process model

**LOCKED:** Native Astra applications use explicit spawn, typed handles, ports,
queued events, and process-level termination. Unix file descriptors, signals,
process groups, and `fork()` do not define the native kernel ABI.

**DIRECTION:** A userspace POSIX personality maps Unix concepts onto native
mechanisms:

| POSIX concept | Astra implementation direction |
|---|---|
| file descriptor | per-process compatibility entry referencing a native stream/file/socket/PTY handle |
| `read`/`write`/`poll` | native object operations plus wait-multiple |
| pipe | bounded shared byte ring plus waitable endpoints |
| `execve` | loader service replaces the process image under an explicit contract |
| `posix_spawn` | native explicit spawn fast path |
| PID and parent/child | compatibility-service identifiers over native process handles |
| process group/session | compatibility-service policy and terminal association |
| `waitpid`/`SIGCHLD` | native process-death wait plus POSIX status translation |
| `errno` | libc translation from stable Astra status values |
| paths | POSIX syntax resolved by the same storage namespace and objects |

The POSIX layer is not a privileged shortcut. It obeys the same rights,
accounting, deadlines, queue limits, and peer-death behavior as native code.

## 6. Fork and subshells

Full zsh semantics use process cloning for subshells, pipelines, command
substitution, jobs, and some builtins. A spawn-only imitation is not enough.

**OPEN, preferred direction:** after basic VM, shared areas, executable loading,
and map/protect tests are stable, add one narrow kernel mechanism to clone a
process address space with copy-on-write mappings and an explicit inherited
handle set. The POSIX personality exposes that mechanism as `fork()`; native
application APIs remain spawn-first and need not expose Unix inheritance.

Required properties include:

- no duplicated authority except the explicitly selected compatibility set;
- identical parent/child user state at the return boundary;
- copy-on-write faults with exact commit accounting and no overcommit;
- cache/ATC maintenance consistent with the MC68030 contract;
- bounded rollback on allocation failure;
- no cloning of in-flight kernel waits or device ownership accidentally;
- performance counters for clone, first-write faults, and reclamation.

The zsh port should use native spawn for simple external commands where a
small, maintainable patch preserves semantics. True subshell behavior still
uses the compatibility clone path. An eager-copy prototype may be useful as a
correctness oracle, but it is not the intended interactive fast path.

This mechanism requires an explicit revision to `KERNEL_SPEC.md` before code;
this document does not silently authorize it.

## 7. Signals and job control

Zsh and Vim require more than process death. The compatibility environment must
define:

- foreground and background process groups;
- stop, continue, interrupt, hangup, terminate, and child-status behavior;
- terminal-generated control events;
- blocked signal masks and ordered pending state;
- safe user-mode delivery and return;
- `SIGKILL` as whole-process termination, never arbitrary thread destruction;
- process-group cleanup when the shell or terminal dies.

Native Astra services continue to use queued events and cancellation rather
than Unix signals. Which minimal kernel mechanisms are needed for suspend,
resume, and user-mode signal delivery is **OPEN**. They must preserve the
kernel's no-forced-thread-destruction and bounded-latency invariants.

## 8. Namespace presentation

Native logical locations may use names such as `SYS:`, `WORK:`, `HOME:`,
`RAM:`, and `APP:`. POSIX programs see one stable slash-path view of the same
underlying storage objects, for example `/System`, `/Work`, `/Home`, `/Ram`,
and the launched bundle directory.

The two syntaxes are views, not duplicate filesystems. `chdir`, desktop
navigation, drag and drop, and native open operations ultimately resolve to the
same object identity and capability checks.

## 9. Porting stages

1. Build a cross libc and configure-probe profile without weakening upstream
   feature tests.
2. Run noninteractive `zsh -f -c` scripts with files, environment, and status.
3. Add interactive ZLE operation on an Astra PTY.
4. Pass redirection, pipes, command substitution, and subshell tests.
5. Pass foreground/background jobs, stop/continue, terminal interrupt, and
   child-death tests.
6. Enable history, completion, selected modules, and startup caching.
7. Run Vim with alternate screen, resize, suspend/resume, files, and crash
   recovery.

Each stage runs under the emulator and RTL/shared platform harness where
practical, then on the ULX3S. Test time, resident memory, binary/module size,
prompt latency, fork/spawn cycles, and terminal input-to-pixel latency are
recorded continuously.

## 10. Distribution policy

- The default zsh configuration stays small and fast.
- Completion metadata is indexed or cached at package installation, not
  rediscovered expensively at every prompt.
- Modules load on demand.
- Third-party frameworks are optional packages, not part of the base boot.
- Shell startup and command lookup never require network access.
- Native Astra commands expose useful text streams and stable machine-readable
  modes without turning the shell into an object-serialization runtime.

## 11. Acceptance behavior

The shell environment is useful when it can:

- open from the desktop with one command and produce a prompt promptly;
- edit Unicode command lines and history without dropped input;
- run native and POSIX-compatible programs;
- redirect, pipe, background, foreground, stop, continue, and interrupt jobs;
- survive one child crash without losing the terminal or desktop;
- run Vim correctly in the alternate screen;
- report bounded memory and queue use after repeated jobs;
- remain interactive while storage, network, and CPU are stressed.

## 12. Upstream references

- zsh source: <https://github.com/zsh-users/zsh>
- zsh jobs and signals: <https://zsh.sourceforge.io/Doc/Release/Jobs-_0026-Signals.html>
- zsh redirection: <https://zsh.sourceforge.io/Doc/Release/Redirection.html>
- zsh job-control options: <https://zsh.sourceforge.io/Doc/Release/Options.html#Job-Control>
