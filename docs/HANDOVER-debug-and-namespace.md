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

`origin/main` is at `d1fef0c`. Three commits are local and unpushed:

| Commit | What |
|---|---|
| `374044b` | the namespace implementation plan |
| `a3caf20` | capability names become bounded strings |
| `75b97fb` | the assign table |

Every gate is green on Beast: 30 kernel suites in both configurations,
userspace test/sanitize/analyze/cross-build, `ext4-test`, the ELF fixture at 84
pages, the terminal gate, and the boot budget at 0.08s of 1.00s. Kernel line
coverage is 82.9% (`cd sw/kernel && make coverage`).

## 2. Resume here

**`docs/superpowers/plans/2026-08-06-namespace-foundation.md`, Task 3.**

Tasks 1 and 2 are done and committed. Task 3 is assign-rooted path parsing —
`astra_path_split` and `astra_path_normalise` — and the plan carries the
complete test file and implementation. It is self-contained: two new files in
`sw/userspace/vfs` plus a Makefile target, nothing existing changes, and it
builds and tests on the Mac.

It is also the piece with the security property in it: `..` at an assign's root
is an error rather than a parent, so no string a program can build escapes the
authority it was given. The tests for that are the point of the task.

After Task 3, the next plan — not yet written — wires the shell and the ext4
backend onto the assign table. That one changes behaviour on the machine;
everything so far only adds to the Kit.

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
3. **The shell still builds `/`-rooted paths** and the ext4 backend still
   prefixes a mount point. Both go when the wiring plan lands.

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
