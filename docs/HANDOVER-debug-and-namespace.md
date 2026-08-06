# Astra 68 — Handover: the debug surface, and the namespace design

Date: 2026-08-06. Written to be read cold in a fresh session.

Two things happened this session. A machine that could barely say anything
about itself got a debugger, a diagnostic channel and a legible fault report;
and the namespace, filesystem layout and event system were designed and
partly built.

`docs/HANDOVER-vfs-and-stacks.md` is the previous resume point and is still
accurate about storage and stacks. **Everything is on `main`** — there are no
other working branches.

---

## 1. Where things stand

`origin/main` is at `d1fef0c`. Everything since is local and unpushed: the two
namespace plans and their five implementation commits, from `a3caf20`
(capability names become bounded strings) to `cbc31d9` (the shell stands in an
assign), plus the spec sections on naming a second volume.

Every gate is green on Beast: 30 kernel suites in both configurations,
userspace test/sanitize/analyze/cross-build, `ext4-test`, the ELF fixture at 84
pages, the terminal gate at six lines, and the boot budget at 0.09s of 1.00s. Kernel line
coverage is 82.9% (`cd sw/kernel && make coverage`).

## 2. Resume here

**Nothing in the namespace plans. Both are complete and both are green on
Beast, including the terminal gate.**

`2026-08-06-namespace-foundation.md` and `2026-08-06-namespace-wiring.md` are
finished. The machine has no `/` any more: a path is `ASSIGN:rest`, the
supervisor binds `SYS:` (read-only, the volume root) and `WORK:` (read/write,
`work/`, created if the volume has none) when it mounts, and the shell stands
in an assign. `write SYS:x` answers *access denied* from the namespace, before
the filesystem is asked -- the terminal gate types that and reads it back off
the screen.

The next thing is a choice rather than a queue. Either the mounter service and
volume identity (spec 1.6, which is where a second volume gets a name), or the
events work in `2026-08-06-event-system-design.md`, which is independent of all
of this. The bundle manifest and the startup manifest both want a launch path,
which userspace still does not have.

**`make analyze` cannot run on the Mac at all** — `ANALYZER_CC=gcc` resolves to
Apple clang, which has no `-fanalyzer`. This predates the namespace work; the
analyze gate is a Beast gate.

## 3. What the machine gained this session

| Capability | Where |
|---|---|
| Source-level debugging | `emu/qemu/debug.sh`, symbols for ROM, kernel and user image |
| A diagnostic channel a program can use | `ASTRA_SYSCALL_LOG_WRITE`, `astra_log()` |
| Assertions that name file and expression | `sw/userspace/runtime/src/assert.c` |
| A legible user fault, with its address classified | `kernel_process_fault_report` |
| Address → function and line | `tools/symbolize.py` |
| A production/development split | `ASTRA_KERNEL_DEBUG_SURFACE` |
| Kernel coverage measurement | `cd sw/kernel && make coverage` |

`docs/DEBUGGING.md` is the developer-facing page for all of it.

Also this session, before the above: the syscall-record alignment fix that
explained the terminal hang, reserve-and-grow thread stacks, and
`emu/qemu/test-terminal.py`, which is the terminal's first end-to-end gate.

## 4. The design, and what it commits us to

Two specs, both pushed, both approved in conversation:

- `docs/superpowers/specs/2026-08-06-filesystem-layout-design.md` — assigns as
  granted capability bindings, `NAME:rest` paths with no root, `SYS:`
  read-only, application bundles, the two launch contexts, one instance per
  application, and the rules that stop a person bricking the machine by editing
  a file.
- `docs/superpowers/specs/2026-08-06-event-system-design.md` — message ids and
  typed arguments rather than text, an activity id that follows a request
  across four processes, five levels, monotonic time with the wall clock as an
  event, and a retention policy built around the failures that bite.

**Three things in the specs contradict what is currently built.** They are
deliberate and they are not yet done:

1. **`LOG_WRITE`'s authority is backwards for an event system.** Emitting is
   gated on `ASTRA_RIGHT_DEBUG` today. The events spec reverses this: every
   process may emit, and the right gates *reading*. If the machine's account of
   what happened depends on a capability, it has holes exactly where something
   went wrong.
2. **`LOG_WRITE` writes to the console, not the trace ring.** The spec requires
   one ordered stream with one set of sequence numbers, with the console as a
   sink on it.
3. ~~The shell builds `/`-rooted paths.~~ **Done.** The shell is assign-rooted
   as of `cbc31d9`. The ext4 backend still prefixes lwext4's own mount point,
   and deliberately keeps doing so: `"/vol/"` is that backend's namespace, not
   the machine's, and the spec's 9 row means *when there are two mounts*.

## 5. Traps this session found

- **The Mac cannot catch an ABI change.** Widening the capability name compiled
  clean on the Mac and failed four ways on Beast: the kernel's own
  `KernelProcessBootstrapCapability` carried a `uint32_t name` of its own, its
  validation compared `name == 0u`, and two kernel tests built fourcc names by
  hand as `0x44455631u /* DEV1 */`. **Any ABI change goes to Beast before it is
  believed.**
- **`git ls-files | tar` does not ship new files.** Untracked files are
  invisible to it, so a build on Beast fails with "No rule to make target" for
  a file that exists on the Mac. Commit first, or `scp` the new files
  separately.
- **`scheduler_stats` moves between builds.** Reading counters through the QEMU
  monitor needs the symbol address from *that* build; `make K1_QUALIFICATION=1`
  leaves a different kernel in the tree than `make` does, and the address
  differs. Re-read it from `astra_kernel.elf` after every rebuild.
- **A fault report is not a fault.** `kernel_process_on_fault` now answers some
  faults by growing a stack, so `user_faults` and `user_stack_growths` are
  separate counters on purpose. A test that resets one and not the other reads
  a stale count from a previous test in the same binary.
- **A colon needs a chord.** The terminal gate types with QMP qcodes and had
  no way to send one, so `SYS:` could not be typed until `chord()` existed.
  Assign names are case-insensitive, which is why `sys:` in lower case is
  enough and no other shifted key is needed.
- **The terminal gate needs its own settle.** `test-terminal.py` waits on the
  screen rather than sleeping, because a fixed two-second settle was flaky
  against TCG: `readdir` reopens the directory per entry, and a listing that had
  not finished read as a hang when it was arithmetic.

## 6. Reproducing the gates

```sh
# userspace, including the assign table
cd sw/userspace && make test && make sanitize && make analyze && make all

# kernel: 30 suites, both configurations
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1

# what the kernel suites actually cover
cd sw/kernel && make coverage

# the terminal, end to end, over QMP
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/astra_boot.bin --image /tmp/part.img

# a debugger with ROM, kernel and user symbols
QEMU=/tmp/qemu-final-build/qemu-system-m68k ./emu/qemu/debug.sh --image /tmp/part.img

# the Mac-only pytest halves
cd sw/boot && python3 -m pytest tests/test_pack_payload.py
python3 -m pytest tools/tests/test_symbolize.py
```

## 7. Open decisions, carried forward

- The shell language, which the startup manifest's syntax will want.
- The bundle manifest: how an application declares the authority it wants, and
  how a person sees and answers that request.
- The event system's numbers — tier budgets, token-bucket rates, boot ring
  size, coalescing window — which want a measured workload rather than an
  opinion.
- Whether an activity should ever be attributed to a person's action, once
  there is an interface to attribute it to.
