# Astra 68 — Handover: quoting, redirection, and an append that was not one

Date: 2026-08-19, continuing `HANDOVER-qualification-and-time.md`. Read
`CLAUDE.md` first.

---

## 1. What this changed

**The shell redirects.** `ls > out.txt` and `date >> log` work, and they work
the way this machine's design says they should: the child is granted a send
handle to a *different port*, whose sink writes into an open file, and it never
learns that anything is unusual about its STDOUT. Nothing about the child
changes, no descriptor table appears, and STDERR stays on the terminal so a
program whose output is in a file can still say it failed.

**The shell already quoted.** `HANDOVER-qualification-and-time.md` §3.2 said it
could not, and `date` printed *"The shell cannot quote yet"* when it saw a
second word. Both were wrong: `astra_shell_parse` has handled `'`, `"` and `\`
since `8006c49`, `run_line` calls it, and arguments reach a child NUL-separated
through `astra_launch_arguments_pack`. Proved on the emulator against the real
ROM before anything was written:

```
date +"%H %M"   ->  01 26     date: exited 0
date +'%H %M'   ->  01 26     date: exited 0
```

The claim is now a gate rather than a comment, and `date`'s message says what
is actually true.

**Every append on the machine silently truncated.** `ext4_backend_open` mapped
create-without-truncate to lwext4's `"wb"`, which creates *and* truncates —
with a comment saying so. That is the mode `>>` opens with, and it is the mode
anything that adds to a file opens with. It is fixed at the backend, not in the
shell, because every caller of `ASTRA_VFS_OPEN_CREATE` without
`ASTRA_VFS_OPEN_TRUNCATE` had the bug.

## 2. Where each piece lives

| Piece | File |
|---|---|
| `>` and `>>` parsed off the line | `sw/userspace/shell/src/shell.c` |
| `redirect` / `redirect_append` on the words | `sw/userspace/shell/include/astra/shell.h` |
| The second sink, and which handle STDOUT grants | `sw/userspace/supervisor/src/console_stream.c` |
| Opening the file, and putting STDOUT back | `sw/userspace/supervisor/src/console_shell.c` |
| Create without truncate | `sw/userspace/vfs/src/vfs_ext4_backend.c` |
| The gate | `emu/qemu/test-terminal.py` |

Three things in that list are worth knowing before touching any of them.

**`>>` is a seek, not an open flag.** The protocol has no append bit and does
not need one: open without `TRUNCATE`, then seek to the end. Adding a flag
would be a second way to say one thing, and the one that races.

**The redirected sink is a second port, so it is a second thing to wait on.**
STDERR still arrives on the terminal's sink, so neither handle stands in for
the other. `pump_once` waits on both; a wait naming only the first sleeps
through a child that filled the second and stopped.

**The builtins are one list now.** They were a chain of `shell_equal` until a
redirect had to be refused on a builtin — which needs the question asked
*before* the answer runs. Two places naming the same six words is one place for
them to drift, so `shell_builtins[]` is the list and the chain is gone. A
builtin writes to the terminal directly and has no stream to move, so a
redirect on one is refused rather than ignored: `ls > out.txt` works because
`ls` is a program.

## 3. What is proved, and how

`emu/qemu/test-terminal.py` — `ASTRA TERMINAL PASS 41 commands`. The new cases
are at the end of `SCRIPT`, deliberately: the two `cat events:activity/N` lines
name an activity by its position in that list, and the file asserts it.

- **Quoting**, by a name that cannot exist without it. `mkdir "two words"` then
  `ls` showing `two words/` — two arguments would have made `two/`. No message
  has to be believed.
- **Redirection**, by absence and then presence. The shell echoes what the
  terminal renders into the event ring, so a redirected child renders nothing
  and its answer is *not* on the screen; `cat out.txt` is where it turns up.
- **Append**, by both answers being in one file.
- **Truncation**, by what is *no longer* in it — which no needle in `SCRIPT`
  can say, because every needle there has to be found. It is a check of its own
  after the loop.
- **The two refusals**: `pwd > out.txt` and `ls >`.
- **The parser**, on the host, in `sw/userspace/shell/tests/test_shell.c`:
  `ls>out.txt` with no spaces, a quoted `>` that is text, a backslashed one,
  `ls >`, `ls > a > b`.

Both halves were made to fail on purpose:

| perturbation | what failed |
|---|---|
| `mode_of` back to `"wb"` for create-without-truncate | the `>>` case, exactly as it did before the fix |
| `console_stream_stdout()` always returning the terminal's handle | `cat out.txt` finds nothing |
| `redirect_append = 0` in the parser | the host test aborts |

## 4. What is left

- **`<` and `2>` are not parsed**, so those characters are still ordinary text.
  `<` is the source side: a file feeding `console_stream_offer` rather than the
  line editor, which is a smaller piece than this one was. `2>` needs only a
  second redirect on the words, since STDOUT and STDERR are already separate
  grants pointing at the same port on purpose.
- **No pipes.** Two children and a port between them, with neither end being
  the terminal. The sink and source already have the right shape for it.
- **`> file` with no command does nothing.** Every other shell creates the
  file; here `argc == 0` returns early. Deliberate, and worth a line if anyone
  relies on it.
- **`write` is still a builtin**, and the reason it existed — there being no
  other way to put bytes in a file — is now gone. `cat` into a redirect does
  it. Removing it is a separate change and wants a replacement for the gate
  lines that use it.
- Everything in `HANDOVER-qualification-and-time.md` §3.1, §3.3, §3.4 and §3.5
  stands: storage and input are still not qualified, `console_printf` still has
  ~480 call sites to convert, and the DE25 Nano still wants its list checked.

## 5. Traps this cost time to find

- **A stale handover is worse than no handover.** §3.2 named the wrong half of
  its own item, and `date` carried the same claim in a user-visible string. Ten
  minutes with a probe against the real ROM was cheaper than a day building
  something that already worked. **Run the machine before believing the
  document, including this one.**
- **`make clean && make -j8 test` in `sw/userspace` fails** with
  `No rule to make target '../../runtime/build/m68k/crt0.o'` — the test target
  does not build the runtime it links against. `make -j8` first, then
  `make test`. Pre-existing, and not the change under test.
- **An append that truncates looks like a short write.** The file that came
  back was the right length for the last write and the wrong length for the
  file, and nothing in the shell, the stream or the sink was wrong.

## 6. How to run it

Per `HANDOVER-qualification-and-time.md` §5, with `make -j8` before `make test`
in `sw/userspace`. Nothing here touches `sw/kernel` or `sw/boot`, so the ROM is
byte-identical and the qualification ROM is unaffected.
