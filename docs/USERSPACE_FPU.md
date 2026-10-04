# Userspace FPU for Astra 68

Status: revision 0.5 (2026-10-04). Phases 0-3 done and measured on the
DE25: userspace is hard float.
Status words follow `KERNEL_ARCHITECTURE.md`: **LOCKED**, **CURRENT**,
**PLANNED**, **MISSING**.

## Decision

User threads use the MC68040's floating-point unit. The owner decided this on
2026-10-03, replacing "Userspace FPU (decided: allowed)" in
`DEVILUTIONX_PORT.md:25` and decision 1 in `CHOCOLATE_DOOM_PORT.md:75-77`.

The kernel stays float-free. It never executes an FPU instruction except the
ones that save and load a user thread's FPU state.

Why: on the DE25, 74% of Chocolate Doom's guest instructions are
compiler.library soft-float, with 54% in `__mulsf3`
(`HANDOVER_2026-10-03_BOARD.md:126-127`). They come from SDL_mixer's float
panning and SDL's float resampler. DevilutionX's audio path is float from end
to end (`DEVILUTIONX_PORT.md:21-25`).

## Summary

| Area | Design |
|---|---|
| Kernel state | Each `KernelThread` holds an FSAVE frame, FP0-FP7 and FPCR/FPSR/FPIAR, plus a second copy for the signal context |
| Switch point | The one exit to user mode, `_kernel_restore_user_context`, with interrupts masked |
| Policy | Switch only when the owner changes. The thread whose state is in the FPU is the owner; returning to it saves and loads nothing |
| Laziness | None at first. The MC68040 has no FPU-disable bit, and QEMU's FSAVE never writes a null frame, so first use cannot be detected |
| FPU exceptions | User FPU traps retire the process with the vector recorded, as every non-recoverable user exception does today. No SIGFPE |
| Unimplemented 040 instructions | Avoided at compile time. Astra's QEMU raises F-line for them so any violation fails under the gates. No FPSP |
| ABI | A flag day. The compiler driver defaults to hard float, objects carry `Tag_GNU_M68K_ABI_FP`, and libraries whose ABI includes a float or double return get a new major version. No multilib |
| Kernel and boot firmware | Stay `-msoft-float`. Neither links libgcc |

## 1. What the code does today

### 1.1 Trap entry and exit (CURRENT)

- Vector table, `sw/kernel/vectors.S:12-27`:
  - vectors 0-1, 3-46, 48-79 and 81-255 go to `_kernel_exception_entry`;
  - vector 2 (access fault) goes to `_kernel_access_fault_entry`;
  - vector 47 (`TRAP #15`, `ASTRA_SYSCALL_TRAP`, `sw/include/astra/syscall.h:8`) goes to `_kernel_syscall_entry`;
  - vector 80 (`KERNEL_IRQ_COMMON_VECTOR`, `sw/kernel/irq.h:16`) goes to `_kernel_interrupt_entry`.

  F-line (11) and all eight FPU vectors (48-55) therefore reach the generic
  exception entry.
- Every entry masks to IPL 7 and pushes D0-D7/A0-A6, then calls a C dispatcher
  with the register block, the raw frame and USP (`vectors.S:33-51`,
  `104-122`, `126-152`). Nothing saves or restores FPU state.
- The dispatcher returns a `KernelCpuContext *` or a worker or idle code.
  Every return to user mode goes through `_kernel_restore_user_context`
  (`vectors.S:159-173`). That includes:
  - the first entry (`kernel_enter_user`, `vectors.S:156-157`);
  - the worker handing back (`kernel_worker_switch_to_user`, `sw/kernel/worker.S:13-23`).

  The routine loads USP and the thread's kernel stack top from
  `KERNEL_THREAD_KERNEL_STACK_TOP_OFFSET(%a0)`, which works because the context
  is the first field of `KernelThread` (`sw/kernel/thread.c:266-269`). It then
  builds a format-0 frame and executes `rte`. **This is the only place a
  thread's register state reaches the CPU. It is where the FPU switch goes.**
- `KernelCpuContext` (`sw/kernel/context.h:30-41`, 76 bytes) holds D0-D7,
  A0-A6, USP, PC, SR, vector, frame format and valid. It has no FPU state.
- `_kernel_restore_user_context` calls `kernel_irqoff_latency_exit`
  (`vectors.S:163`) before it restores anything. Work added after that call
  runs with interrupts masked but is not measured.

### 1.2 Threads, signals, fork, reaping (CURRENT)

- **Thread record.** `KernelThread` (`sw/kernel/thread.h:144-229`) is 376
  bytes under `m68k-elf-gcc -m68040 -msoft-float` (measured 2026-10-03). It
  lives in its own whole-page record: frames are
  `ceil(sizeof / KERNEL_PAGE_SIZE)` (`thread.c:201-214`), and
  `thread.c:271-272` asserts it fits one 4 KiB page. FPU state therefore costs
  no frames. The budget tables at `MEMORY_BUDGET.md:415` and `:518` (180 and
  192 B per record) and `KERNEL_ARCHITECTURE.md:202` (184 B) were already out
  of date.
- **Signals.** The kernel delivers a signal on the same thread.
  `signal_deliver` (`sw/kernel/process.c:1066-1095`) copies `thread->context`
  into `thread->signal_saved_context` (`thread.h:228`), pushes two words onto
  the signal stack, and points PC at the process trampoline.
  `ASTRA_SYSCALL_SIGNAL_RETURN` copies it back (`process.c:11589-11598`). The
  trampoline is C, `astra_posix_signal_trampoline`
  (`sw/userspace/posix/src/signal.c:105`). With hard float, a handler
  compiled C may clobber FP0-FP1 and FPCR/FPSR, and the interrupted code would
  see it. Signal delivery must save FPU state.
- **Fork.** `clone_current_process` (`process.c:3885`) calls
  `prepare_cloned_thread` (`process.c:3800-3845`). That copies the signal
  context (`3828-3832`) and the integer context (`3842`) from the forking
  thread. The forking thread is the FPU owner, so its live FPU state is in the
  CPU, not its record. Fork must flush before copying.
- **Thread delivery runs from `runtime_resume`** (`process.c:1141-1149`), the
  common path that every dispatcher uses to pick the context to return.
- **Reaping.** Thread records are freed by `release_thread_record`
  (`thread.c:246-264`), the one release point for every caller
  (`thread.c:1205`, `1298`, `1329`, `1341`, `1354`, `1404`, `1585`, `2435`).
  The worker reaps with interrupts enabled. The reaper's
  `kernel_vm_deactivate` race (`process.c:1518-1531`, commit `06b30f8a`) is
  the precedent for any lazily held hardware state that names a thread or a
  space.

### 1.3 User exceptions (CURRENT)

- `kernel_exception_entry_dispatch` (`sw/kernel/dispatch.c:291-301`) panics on
  a kernel-mode exception. For a user exception it calls
  `kernel_process_on_fault` (`process.c:13311-13362`).
- An access fault that grows the stack, breaks copy-on-write or maps an area
  resumes the thread. **Every other user exception records `fault_vector`
  and retires the process with `KERNEL_PROCESS_EXIT_USER_FAULT`.** F-line and
  vectors 48-55 already get this answer.
- The frame decoder accepts formats 0, 1, 2, 3 and 7
  (`sw/kernel/exception.c:56-71`), which covers the 68040 FPU frames:
  format 0 for pre-instruction, 3 for post-instruction, 2 for unimplemented
  F-line. Check this list against M68040UM chapter 9 when implementing.

### 1.4 Nothing uses the FPU today (CURRENT)

`git grep` for `fsave`, `frestore`, `fmove` and `fmovem` in `sw/` finds
nothing. The kernel never initialises the FPU (`sw/kernel/entry.S`).

## 2. Kernel FPU context (PLANNED)

### 2.1 State layout

Add a `KernelFpuContext` to `KernelThread`, plus a second one for the signal
context:

| Field | Bytes | Contents |
|---|---:|---|
| `frame` | 100 | FSAVE state frame. On the 68040 this is null, idle (4 B), unimplemented-instruction or busy. Reserve the busy size. **Unverified:** whether the busy frame is 96 B plus the 4 B header or 96 B in total. Take the exact size from M68040UM §9 and assert it |
| `data` | 96 | FP0-FP7, 12-byte extended each |
| `control` | 12 | FPCR, FPSR, FPIAR |

That is 208 bytes per context and 416 per thread. The record grows from
376 to about 792 bytes and still fits the one page it already occupies, so
the frame budget is unchanged. Give the offsets `#define`s and
`_Static_assert`s, the way `context.h:4-12` and `thread.h:7` do, so the
assembly cannot drift.

The `KernelCpuContext` layout and size (76) do not change. FPU state is a
separate field because `KernelCpuContext` is copied wholesale by the signal,
fork and capture paths, and those paths need to handle FPU ownership
explicitly anyway.

### 2.2 Switch policy: owner tracking (eager, no first-use trap)

One kernel global, `kernel_fpu_owner`, holds the `KernelThread *` whose state
is in the FPU, or NULL. In `_kernel_restore_user_context`, D0 already holds
`&thread->context`, which equals `thread`:

```
    cmp.l   kernel_fpu_owner, %d0
    beq     same                     | syscall return, interrupt return: no FPU work
    move.l  kernel_fpu_owner, %d1
    beq     load                     | no owner (boot, or owner died/invalidated)
    move.l  %d1, %a1
    fsave   FPU_FRAME(%a1)           | waits for the FPU, captures internal state
    fmovem.x %fp0-%fp7, FPU_DATA(%a1)
    fmovem.l %fpcr/%fpsr/%fpiar, FPU_CONTROL(%a1)
load:
    move.l  %d0, %a1
    fmovem.l FPU_CONTROL(%a1), %fpcr/%fpsr/%fpiar
    fmovem.x FPU_DATA(%a1), %fp0-%fp7
    frestore FPU_FRAME(%a1)
    move.l  %d0, kernel_fpu_owner
same:
```

This is Motorola's order: FSAVE first, then the programmer's model on save;
the programmer's model, then FRESTORE, on restore. A trial build confirms the
current kernel flags (`-m68040 -msoft-float`, which passes `-mno-float -mcpu=68040` to
gas) still assemble `fsave`, `fmovem.x` and `fmovem.l`. The kernel can carry
these in `vectors.S` without changing its float ABI.

Put the switch before `kernel_irqoff_latency_exit` (`vectors.S:163`), so the
interrupts-off latency instrumentation counts it.

**Why not lazy:**

- **Trap on first use.** The 68040 has no coprocessor-disable bit, so there is
  nothing to trap on.
- **Null-frame laziness, as Linux m68k does it.** Linux FSAVEs and skips
  FMOVEM when the frame is null, meaning no FPU instruction has run since
  reset. Astra's QEMU makes this inert:
  - FSAVE always writes an idle frame `0x41000000`
    (`target/m68k/translate.c:5381-5391`, upstream QEMU 9.2.4);
  - FRESTORE ignores its operand ("FIXME: check the state frame",
    `translate.c:5365-5378`).

  So the null case never occurs, and a design that relied on FRESTORE of a
  null frame to reset the FPU would hand a new thread the previous owner's
  registers.
- **A per-image "uses FPU" flag in ELF.** Every hard-float library would set it,
  so it adds nothing.

Owner tracking gives most of the laziness for free. Syscall returns,
interrupt returns that resume the same thread, and the idle and worker round
trips (`kernel_enter_idle`, `worker.S`) do no FPU work. The cost is paid only
when a different thread resumes, including threads that never use the FPU.
Measure it with the IPC benchmark (`~/ipc-prof.sh`) before and after
(section 6, phase 1).

**Optional later optimisation (PLANNED, needs owner approval).** Give Astra's
QEMU Motorola's null-frame behaviour:

- FRESTORE of a null frame resets the FPU;
- FSAVE yields null until the next FPU instruction.

This follows the "Motorola is the authority" rule. After that, the kernel can
skip FMOVEM for threads that have not touched the FPU. Do it only if phase 1
measurements show switch cost matters. The fresh-thread gate (5.2) catches a
kernel that assumes it on an unpatched QEMU.

### 2.3 Fresh state

`kernel_memory_alloc_zeroed_tagged` already zeroes the record
(`thread.c:204-207`). At thread creation, set `frame` to an idle frame
(`0x41000000`) and leave the data at +0.0, with FPCR=FPSR=FPIAR=0. Do not use
a null frame: under QEMU, FRESTORE of a null frame does not reset the FPU
(2.2), so the new thread would inherit the previous owner's registers. That is
a cross-process information leak.

A new thread in the same process copies the creator's FPCR, as C11 7.6
requires: a new thread's floating-point environment starts as its creator's.
The creator must be flushed first. A new process starts with the default
state.

### 2.4 Ownership rules

`kernel_fpu_owner` is lazily held hardware state that names a thread, the same
shape as `current_user_root`. Three rules:

1. **Only two kinds of code write `kernel_fpu_owner`:**
   - the restore path, which runs at IPL 7;
   - three helpers that mask interrupts themselves with
     `kernel_interrupt_save_disable` / `kernel_interrupt_restore`
     (`vectors.S:209-237`).

   The helpers:
   - `kernel_fpu_flush(thread)`: if `thread` is the owner, save the FPU into
     its record and keep ownership (the registers still match);
   - `kernel_fpu_invalidate(thread)`: if `thread` is the owner, set the owner
     to NULL without saving, so the next resume loads from the record;
   - `kernel_fpu_release(thread)`: invalidate, called from
     `release_thread_record` (`thread.c:246`) before the record is freed.

   Without the third, the next switch FSAVEs into a freed, possibly reused
   frame. That is silent memory corruption, and it would read like an
   allocator bug.
2. **The kernel never executes an FPU instruction outside these helpers and
   the restore path.** It is built `-msoft-float` and links no libgcc
   (CLAUDE.md, "The kernel links no libgcc"). Its C code cannot emit FPU
   instructions, so user FPU registers survive kernel execution untouched. That
   includes interrupts taken in the kernel, the worker
   (`worker.S`, `worker.c`) and idle. The interrupt and exception entries need
   no FPU save, which keeps interrupt latency unchanged. Enforce it at build
   time (5.3).
3. **Any code that reads or writes a thread's FPU record calls
   `kernel_fpu_flush` first. Code that writes it then calls
   `kernel_fpu_invalidate`.** The callers:
   - fork (`prepare_cloned_thread`): flush the source, copy both FPU contexts
     alongside `process.c:3828-3842`;
   - signal delivery (`signal_deliver`): flush, copy the FPU context to the
     signal copy, reset the live FPCR/FPSR to 0 so the handler runs in the
     default environment, invalidate;
   - `ASTRA_SYSCALL_SIGNAL_RETURN`: copy the signal FPU context back,
     invalidate;
   - thread creation (2.3);
   - a future debugger or fault-report path.

**Interrupts and the worker.** An interrupt cannot change the FPU hardware.
It also cannot change `kernel_fpu_owner`, because rule 1 confines the writes
and an interrupt taken while the worker runs does not return to user mode:
`kernel_process_worker_resume` (`process.c:8040`) and
`kernel_worker_switch_to_user` (`worker.S:13`) are the only way back. The
`06b30f8a` race needed a read-compare-switch spanning an interrupt that
installs state. Here the switch happens only at IPL 7, and the helpers mask.

**Signals and the user signal frame.** The integer state is kept in the
kernel (`signal_saved_context`), not on the user stack. FPU state follows the
same model, so the user-visible signal ABI does not change. Nested signals
are already refused (`signal_context_active`, `process.c:1074-1077`), so one
saved copy is enough.

### 2.5 Costs

- **Memory:** 416 B per thread, inside the existing page-sized record. No
  frames, no new pool.
- **Time per owner change:** one FSAVE, two FMOVEM.X of 8 registers and two
  FMOVEM.L of 3 registers. Under TCG these are helper calls copying
  `floatx80` values. The cost is unmeasured; phase 1 measures it with the IPC
  benchmark and a two-thread ping-pong. Syscall and same-thread interrupt
  returns add one compare and branch.

## 3. FPU exceptions and unimplemented instructions

### 3.1 What the MC68040 does (architectural authority)

The 68040 FPU implements in silicon:

- FMOVE, FMOVEM, FADD, FSUB, FMUL, FDIV, FSQRT, FABS, FNEG, FCMP, FTST;
- FSGLMUL and FSGLDIV;
- the single- and double-rounding variants (FSxxx and FDxxx);
- FBcc, FScc, FDBcc, FTRAPcc, FNOP;
- FSAVE and FRESTORE.

Everything else is unimplemented: FSIN, FCOS, FTAN, FSINCOS, FATAN, FETOX,
FLOGN and the other transcendentals, FINT, FINTRZ, FGETEXP, FGETMAN, FMOD,
FREM, FSCALE and FMOVECR. These take the F-line vector (11) with a format-2
frame. The packed-decimal data type and denormalised or unnormalised operands
trap as unimplemented data type (vector 55). Motorola's FPSP (Floating-Point
Software Package) emulates all of these. Arithmetic exceptions enabled in
FPCR trap through vectors 48-54 (BSUN, INEX, DZ, UNFL, OPERR, OVFL, SNAN).
Verify the frame formats and vector numbers against M68040UM chapter 9 before
implementing.

### 3.2 What Astra's QEMU does

From upstream 9.2.4 `target/m68k`, as unpacked by `emu/qemu/prepare-source.sh`
under `~/.cache/astra68/qemu-9.2.4/source-*` on `beast`. The overlay patches
in `emu/qemu/qemu-9.2/patches/` do not touch the FPU.

- **The FPU is present.**
  - `astra68.c:6678-6686` accepts only `m68040`.
  - `m68040_cpu_initfn` (`cpu.c:237-244`) inherits `M68K_FEATURE_FPU` from
    `m68020_cpu_initfn` (`cpu.c:195`) through `m68030_cpu_initfn`
    (`cpu.c:211-217`).
  - Nothing in `emu/qemu/qemu-9.2/hw/m68k/astra68.c` disables it.
- **Instructions the 68040 lacks run natively.** `fsin` (`translate.c:5043`),
  `fmod`, `frem`, `fscale`, `fgetexp`, `fsincos` (`5094-5155`), `fintrz`
  (`5013`) and `fmovecr` (`4954-4959`) all execute without trapping.
- **Packed decimal is the only unimplemented-data-type trap.**
  `EXCP_FP_UNIMP` is raised for it (`translate.c:975`, `1022`). Denormals do
  not trap.
- **Arithmetic exceptions never trap.** `fpu_helper.c` raises no `EXCP_FP_*`.
  Those names appear only in `op_helper.c:159-171`, the exception-name table.
  FPCR enable bits are stored (`fpu_helper.c:148-150`) and ignored.
- **FSAVE and FRESTORE are stubs.** FSAVE always writes idle `0x41000000`;
  FRESTORE ignores its operand (`translate.c:5365-5391`). Both raise privilege
  violation from user mode.
- **FPIAR reads 0 and ignores writes** (`translate.c:4731-4747`).

### 3.3 Options for unimplemented instructions

| Option | Effect | Cost |
|---|---|---|
| A. Compile-time avoidance | GCC `-m68040` emits no unimplemented instruction unless `-funsafe-math-optimizations` / `-ffast-math`. Trial on `m68k-elf-gcc 16.2.0`: `sin` and `exp` are library calls; `-ffast-math` emits `fsin.x`. On 68040 constants load as `fmove.d #imm`, where `-m68020` emits `fmovecr`. `(long)x` uses `fmove.l` with an FPCR round-to-zero dance, not `fintrz`. The libm transcendentals are picolibc C. No Astra build uses `-ffast-math` or `-Ofast` today (`git grep`) | Cannot exclude denormal operands, a data property |
| B. Kernel FPSP | Exact 68040 semantics | About 10k lines of Motorola assembly in the kernel, so the kernel is no longer float-free. Contradicts the LOCKED kernel rule. Dead code on the only CPU Astra runs on |
| C. User-mode FPSP (kernel forwards vectors 11/55 to a per-process handler) | Exact semantics, kernel stays float-free | A new signal-like delivery path and a large userspace library, for traps QEMU never raises |

**Recommendation: A, enforced in three places.**

1. **Toolchain.** The Astra GCC patch removes the 68040-unimplemented
   patterns: make the `sin`/`cos` and similar `m68k.md` conditions require
   `!TARGET_68040`. `-ffast-math` then stays safe for ports.
2. **Emulator.** Astra's QEMU raises F-line (vector 11, format 2) on
   M68K_FEATURE_M68040 for the instructions in 3.1 and for FMOVECR. This is a
   translate-time check in a new overlay patch next to
   `target-m68k-pflush-global.patch`. It is cheap and it is Motorola behaviour.
   A violating binary then dies under every gate with `fault_vector` 11,
   instead of passing under QEMU and failing on silicon.
3. **Gate.** A scan of every shipped ELF's text for coprocessor-1 opcodes that
   are unimplemented (5.3).

**Accepted divergence (LOCKED once approved):**

- Denormal operands and FPCR-enabled arithmetic traps are not emulated. On a
  real 68040 they would retire the process. Under Astra's QEMU, the only
  MC68040 Astra runs on (CLAUDE.md, "The boards"), they compute IEEE results.
- Astra userspace runs with FPCR enables = 0, the IEEE default non-trapping
  mode. `feenableexcept` should report failure rather than pretend.
- No SIGFPE. An FPU trap retires the process like any other user exception
  (1.3), with the vector in the fault report.

## 4. Toolchain and ABI

### 4.1 What changes between `-msoft-float` and hard float

From trial compiles with `m68k-elf-gcc 16.2.0 -m68040`:

- **Arguments.** float and double still go on the stack in both ABIs.
- **Returns change.** float, double and long double come back in `%fp0`
  instead of `%d0` / `%d0:%d1`. This is the binary incompatibility.
- **Register convention.** FP2-FP7 are callee-saved and FP0-FP1 scratch. GCC
  saves FP2 with `fmovem` in a hard-float prologue. Soft-float code never
  touches FP registers, so mixing is unsafe only through returns, not through
  register clobbers.
- **long double** is 12 bytes with a 64-bit mantissa in both ABIs.
- **`__FLT_EVAL_METHOD__` is 0 in both.** The 68040 uses FSxxx and FDxxx
  rounding forms, so there is no x87-style excess precision.
- **The hard-float build defines `__HAVE_68881__`.** picolibc keys its m68k
  `fenv.h` (`third_party/picolibc/libc/machine/m68k/machine/fenv.h:46,70`)
  and `_SUPPORTS_ERREXCEPT` (`libc/include/machine/ieeefp.h:201-203`) on it.
- **Mismatches are not caught.** GCC 16.2 emits no `.gnu_attribute` for m68k,
  so today a soft-float and a hard-float object link silently. binutils does
  support `Tag_GNU_M68K_ABI_FP` (tag 4). Two objects tagged `4,1` (hard) and
  `4,2` (soft) fail to link: `h.o uses hard float, s.o uses soft float`
  (trial, `m68k-elf-ld`).

### 4.2 What hardcodes soft float

- **The compiler itself.** `toolchain/patches/gcc-16.2.0-astra.patch:44-45`:
  `DRIVER_SELF_SPECS "-ffixed-a4 -msoft-float"`. The self spec wins over the
  command line: `m68k-astra-gcc -O2 -mhard-float` on `beast` still emits
  `jsr __mulsf3`. On `beast`, `-print-multi-lib` prints only `.;`, so there
  is no multilib.
- **User ABI flags.**
  - `mk/m68k-cross.mk:32`, `ASTRA_TARGET_ABI_FLAGS`. It is referenced 83
    times across the tree, including the supervisor in `sw/boot/Makefile:26`.
  - `ndk/make/astra-native.mk:9`, `ASTRA_ABI_FLAGS`.
  - `ndk/Makefile:11`, `CPU_FLAGS`.
- **picolibc.**
  - `third_party/picolibc/scripts/cross-m68k-astra.txt:7`;
  - its check `mk/test-picolibc-target.sh:11`;
  - the vendor note `third_party/picolibc/ASTRA_VENDOR.md:28-35`.
- **Supervisor-mode code that stays soft (correctly):**
  - `sw/kernel/Makefile:110`, the kernel;
  - `sw/boot/Makefile:40`, the boot firmware;
  - `emu/qemu/benchmark-host-channel-raw.mk:10`, a bare-metal benchmark.

  None links libgcc.
- **Documents:**
  - `KERNEL_ARCHITECTURE.md:20-21`;
  - `USERSPACE_RUNTIME.md:168`;
  - `OS_VISION.md:210`;
  - `USERSPACE_BUDGET.md:20`;
  - `CHOCOLATE_DOOM_PORT.md:75`;
  - `DEVILUTIONX_PORT.md:25`;
  - `ndk/docs/source/api/pcm.md:42`;
  - `AUDIO_ARCHITECTURE.md:154,175,193`.
- **Toolchain tests.**
  - `toolchain/test-gcc-driver.sh` checks the CPU and does not check float. It
    needs a hard-float assertion.
  - `toolchain/test-fpgnulib-*.sh` test the soft-float helpers on the host.
    They stay valid for libgcc's code but lose their reason once nothing calls
    the helpers.

### 4.3 Toolchain changes (PLANNED)

1. **Make hard float the default.** In `astra.h`, set `DRIVER_SELF_SPECS` to
   `"-ffixed-a4"` and let the `-m68040` default select hard float. The trial
   `m68k-elf-gcc -m68040` defines `__HAVE_68881__`. Verify `m68k-astra-gcc`
   does the same after the rebuild with `-dM -E | grep __HAVE_68881__`. An
   explicit `-msoft-float` from the kernel and boot Makefiles keeps working.
   Ports with their own build systems inherit the right ABI from the driver;
   this is why the default lives in the driver and not only in `mk/`.
2. **Tag every object.** Emit `.gnu_attribute 4, 1` for hard float and
   `4, 2` for soft. Wrap `TARGET_ASM_FILE_END`, which `astra.h` already
   overrides, or add a file-start hook. Untagged hand-written `.S` objects stay
   compatible with both. **Unverified:** whether `ld` merges the attribute from
   shared libraries as well as relocatable objects. Test it during phase 2. If
   it does not, the ELF scan in 5.3 covers the gap.
3. **Drop the unimplemented-instruction patterns** for `TARGET_68040` (3.3).
4. **Rebuild libgcc, libstdc++ and picolibc hard-float.** Check that the soft
   helpers (`__mulsf3`, `__adddf3`) are no longer referenced by anything built
   hard-float.
   - **Unverified:** which libgcc float routines `t-floatlib`'s
     `fpgnulib.c` and `lb1sf68.S` still produce under `__HAVE_68881__`. The
     `compiler.library` export contract
     (`sw/userspace/compiler/Makefile`, `tools/generate_archive_contract.py`)
     shows the difference mechanically.
5. **No multilib.** The only soft-float code is supervisor-mode and links no
   libgcc, libc or libm. A multilib would let soft-float user code keep
   building, and a flag day is meant to stop that.
6. **Fix picolibc's `setjmp`.** `libc/machine/m68k/setjmp.S:26-44` saves only
   D2-D7/A2-A6. FP2-FP7 are saved only by the separate `setjmp_68881` under
   `#ifdef M68881` (`:47-75`). `_JBLEN` is 34 longs
   (`libc/include/machine/setjmp.h:80`), exactly enough for 64 + 72 bytes.
   Under `__HAVE_68881__`, the plain `setjmp`/`longjmp` must save and restore
   FP2-FP7, or any `longjmp` loses callee-saved FP registers. Lua's error
   handling, which is all doubles, is the first to break this way. Record the
   change in `ASTRA_VENDOR.md`. The DWARF unwinder needs no change, because GCC
   emits CFI for saved FP registers.

### 4.4 Library identity and versions

Only a function returning float, double or long double changes binary
meaning. `git grep` for `float|double` across `sw/**/*.h` finds no Astra-native
public header using either. runtime, system, interface, graphics, pcm, font,
filesystem, streams, network and config have no FP in their ABI.

The libraries whose ABI does change:

| Library | Why |
|---|---|
| `compiler.library.1` | Its exported soft-float helpers become dead or disappear. The export contract changes |
| `libc.library.2` | picolibc math and conversion functions return in FP0 (libm is in `LIB_SPEC`, `gcc-16.2.0-astra.patch:80-83`; check whether libm objects are inside `libc.library`) |
| `cxx.library.1` | libstdc++ functions such as `std::stod` and `<cmath>` return double |
| `lua.library.5` | `lua_Number` is double |
| `SDL2.library.2`, `SDL2_mixer.library.2`, and any other SDL add-on whose exports return float | For example `SDL_sinf`, the float render APIs, and mixer effect parameters |

**Recommendation:**

- Bump the major of each library whose export list contains a float or double
  return: 1 to 2 or 2 to 3. Kits record name, ABI major, exact version,
  target architecture and digest (`APPLICATION_AND_KIT_MODEL.md:198-202`).
  With the bump, a stale soft-float application left on a storage volume fails
  to launch with a missing identity instead of reading garbage from D0.
- Name the float ABI in the Kit identity's target architecture (for example
  `m68k-68040-hardfp`).
- **Alternative:** keep the sonames, because every image is rebuilt on the
  flag day and there are no external binaries. That is cheaper but silent on
  the failure mode above. Owner decision.

### 4.5 Flag-day rebuild

Order matters (CLAUDE.md, the supervisor and userspace static libraries trap):

1. Rebuild and install the toolchain on `beast` with 4.3 applied. Back up the
   prefix the way `~/astra-toolchain/backup-*` already does.
2. Rebuild picolibc with the updated cross file.
3. Move every `build/` tree aside (as the `DT_GNU_HASH` change required), then
   rebuild userspace with `make -j8` before `make test`, then `sw/boot`.
   Regenerate every image.
4. Fresh-splice the storage images. Every application on a volume is
   soft-float until rebuilt, and section 4.4 is what makes that loud.
5. Rebuild QEMU with the F-line overlay (`emu/qemu/build.sh host` and `arty`).
   Publish with `emu/qemu/publish-de25-release.sh`.

## 5. Tests and gates

### 5.1 Host unit tests (`sw/kernel/tests`)

The kernel host build already replaces hardware with standalone stubs
(`KERNEL_THREAD_STANDALONE_HOST`, `thread.c:183-197`). Add a host model of
"the FPU": a global register image that the stub `kernel_fpu_save` and
`kernel_fpu_load` copy. Tests:

- **Thread creation.** `test_thread.c`: a new thread's record holds an idle
  frame and zeroed registers. A same-process thread copies the creator's FPCR
  after a flush.
- **Owner release.** `test_thread.c`: releasing the owner's record clears
  `kernel_fpu_owner`. Releasing a non-owner leaves it alone.
- **Fork.** `test_process.c`: the child's FPU context equals the parent's
  *live* model state (dirty it after the last switch), not its stale record.
  Signal-context FPU state is copied when `signal_context_active`.
- **Signals.** `test_process.c`: delivery saves live state into the signal
  copy and gives the handler FPCR=0. `SIGNAL_RETURN` restores it exactly, and
  the owner is invalidated so the next resume reloads.
- **Layout.** `test_context.c`: `_Static_assert`s on offsets, plus a size
  check that the record still fits one page.

### 5.2 QEMU gate: `emu/qemu/test-fpu.py` (new)

A user command, for example `sw/userspace/commands/fpucheck`, driven like
`posix -R` through `test-terminal.py`. It uses inline assembly only:
`fmove`/`fmovem` with distinct bit patterns, and `fmove` to FPCR. The trial
compile shows a `-msoft-float` build assembles FPU instructions, so the gate
does not depend on the ABI flip and can land before it. Subtests, each
printing a named PASS or the first mismatching register:

1. **Threads.** Two threads each load FP0-FP7 and FPCR (rounding mode,
   precision) with their own pattern. Each spins long enough for many timer
   quanta, verifying its registers every iteration.
2. **Processes.** The same across two processes.
3. **Interrupt load.** Subtest 1 while audio and display interrupts run, as in
   `test-sdl-audio`'s load.
4. **Fork.** The parent loads a pattern and forks. The child sees the pattern,
   overwrites all registers and exits. The parent still sees its pattern.
5. **Signals.** A handler overwrites FP0-FP7 and FPCR. The interrupted loop
   sees no change after return, and the handler observed FPCR=0 on entry.
6. **Fresh thread.** A new thread created right after another process left a
   pattern in the FPU reads zero registers and FPCR=0, never the pattern. This
   is the cross-process leak check.
7. **Contract.** `fsin` executed from user mode retires the process with
   `fault_vector` 11. Needs the QEMU overlay; before it, this subtest expects
   success and says so.
8. **Privilege.** `fsave` executed from user mode retires the process with
   vector 8, privilege violation.

**Perturbation (required by CLAUDE.md).** Each must be run and the expected
failure seen:

| Perturbation | Expected failure |
|---|---|
| Today's kernel, the baseline: land the gate first | Subtests 1-6 fail with mismatches |
| Phase-1 kernel built with the switch compiled out (`KERNEL_FPU_SWITCH=0`) | Subtests 1-6 fail |
| `kernel_fpu_flush` removed from fork | Subtest 4 fails |
| Signal FPU copy removed | Subtest 5 fails |
| Fresh state left as a null frame | Subtest 6 fails |
| QEMU without the F-line overlay | Subtest 7 reports the instruction ran |

A `KERNEL_AUDIT=1` build checks at every switch that `kernel_fpu_owner` is
NULL or a live, occupied thread record (`sw/kernel/audit.h`). This catches a
missing release-clear that the gate can only see as delayed corruption.

### 5.3 Build-time checks

- **The kernel stays float-free.** A `tools/` script disassembles
  `sw/kernel/build/kernel.elf` and fails if any coprocessor-1 opcode
  (`0xF2xx` / `0xF3xx`) appears outside the `kernel_fpu_*` symbols and
  `_kernel_restore_user_context`. Coprocessor-2 opcodes (`0xF4xx`-`0xF6xx`)
  are the 68040's CINV, CPUSH, PFLUSH, PTEST and MOVE16 and are allowed.
- **No unimplemented 68040 FPU instructions ship.** A scan of every ELF in the
  image and NDK for the instructions listed in 3.1 and for packed-decimal
  operands.
- **ABI tags.** `readelf -A` on every shipped object and image: hard float or
  no tag, never soft. No undefined reference to soft-float helpers in
  userspace images.
- **Compiler driver.** `toolchain/test-gcc-driver.sh` asserts:
  - `__HAVE_68881__` is defined;
  - `float f(float a,float b){return a*b;}` compiles to `fsmul`;
  - an object carries `Tag_GNU_M68K_ABI_FP: hard float`;
  - `-ffast-math` code contains no `fsin`.
- **picolibc cross check.** `mk/test-picolibc-target.sh:11` checks the new
  flags.

## 6. Phased plan

**Phase 0: measure (no commitment).** Trial on `beast` with stock
`qemu-m68k-static` 8.2.2 (x86-64 host, user mode, stock m68k-elf libgcc). The
loop runs 16.4M iterations of load, `*gain + 0.5`, store and accumulate:

| Build | Time |
|---|---:|
| Hard float | 1.04 s |
| Soft float | 2.79 s |

That is **2.7x, not 10x**. Every FPU instruction is a TCG helper call into
`floatx80` softfloat, plus FPSR bookkeeping. Before the flag day, repeat this
on the DE25 with Astra's QEMU 9.2.4 `arty` build and Astra's patched helpers
(`HANDOVER_2026-10-03_BOARD.md:131` lists faster `__mulsf3`/`__divsf3`, not
yet deployed):

- one static hard-float program as the only FPU user, which runs correctly
  even on today's kernel;
- the same program soft-float.

The profile plugin counts guest instructions, and one FPU instruction costs
far more host time than an integer one. **Judge by host time or the guest
cycle counter, not instruction share.** The Doom audio gain is roughly the
soft-float share of host time multiplied by (1 − 1/speedup).

**Phase 0 result (2026-10-03, DONE).** `toolchain/bench-fpu-ops.sh` builds
`toolchain/bench-fpu-ops.c` hard float and soft float (Astra's patched
libgcc, and the distribution's lb1sf68 for reference), builds `qemu-m68k`
user mode from the Astra 9.2.4 source for AArch64, and times it on the DE25
pinned to CPU 3, an A76, with `astra.service` stopped. Host ns per element:

| Kernel | Hard | Astra helpers | lb1sf68 | Helpers / hard |
|---|---:|---:|---:|---:|
| `pan` (`_Eff_position_s16msb`: s16 → float, two multiplies, → s16) | 226 | 609 | 1462 | 2.69 |
| `muladd` (`x * g + 0.5f`) | 176 | 508 | 878 | 2.89 |
| `div` | 229 | 406 | 800 | 1.77 |
| `dmuladd` (double) | 180 | 1894 | 1432 | 10.5 |
| `int` (the pan loop in integers) | 16 | 16 | 16 | — |

- The FPU and Astra's helpers agree bit for bit on every kernel; lb1sf68
  does not (it does not round correctly). The script fails if the first two
  ever differ.
- One emulated FPU instruction costs about 40 ns on the A76, about 20
  integer instructions. That, not the helpers, now bounds the gain; a
  host-float fast path in QEMU's m68k FPU helpers would raise it. Not
  profiled yet: `beast` has `perf_event_paranoid` 4 and no passwordless sudo.
- Unpinned, the benchmark lands on an A55 and runs about 3x slower again;
  any board timing must name its core.
- Doom on release `635131e4` (fast helpers): soft-float is 62% of guest
  instructions (73% before them), the vCPU never idles, and the soft-float
  is single precision. With `pan`'s 2.7x, hard float frees about
  0.62 × (1 − 1/2.7) ≈ 39% of the vCPU.
- The fast helpers raised Doom 5% (gen 405-408 → 425-430 per 30 s) and
  raised audio gaps 14% (414-424 → 471-480 per 30 s), two runs each with
  `/data/ab-run.sh`. A saturated vCPU shares itself differently; it is not
  a fix for the gaps.

Decision: go (owner, 2026-10-04: start the hardware FPU).

**Host-float fast path (2026-10-03, DONE in the emulator).**
`emu/qemu/qemu-9.2/target-m68k-host-float.patch` makes the A76 do the
arithmetic that QEMU did in `floatx80` softfloat (97% of the time an FPU
instruction took, by `perf` on the board):

- FSADD/FSSUB/FSMUL/FSDIV and the FD forms use host `float`/`double` when
  FPCR rounds to nearest, both operands are exact in the format and the
  result is normal there. Inexact comes from an FMA residual (multiply,
  divide) or the TwoSum error (add, subtract); no other flag can arise on
  those terms.
- FMOVE to and from single and double, FSMOVE/FDMOVE, and int32 to and from
  extended become bit repacks when the value is exact, with the int32
  rounding done on the significand in all four modes.
- Everything else -- other rounding modes, operands with more precision or
  range, zeros, denormals, infinities, NaNs, near-overflow, single-precision
  FPCR where floatx80 rounds a widened value -- keeps the floatx80 path.

`emu/qemu/test-host-float.sh [PAIRS] [BOARD]` builds `qemu-m68k` user mode
with and without the patch and requires the same checksum of every result
image and FPSR across 2M random and edge pairs × 6 FPCR modes × 13
operations; with BOARD it also runs the A76 build. It passes on x86 and
the A76. Perturbations seen failing: dropping the single-precision inexact
raise, and breaking round-half-even in the int32 conversion. The gate
also caught two real divergences during development: QEMU rounds an
`fmove.d` load and an int32 load to 24 bits when FPCR selects single
precision.

DE25 A76, host ns per element, hard float:

| Kernel | floatx80 only | Host float | Astra helpers / host float |
|---|---:|---:|---:|
| `pan` | 226 | 128 | 4.7 |
| `muladd` | 176 | 91 | 5.6 |
| `div` | 229 | 73 | 5.5 |
| `dmuladd` | 180 | 96 | 19.7 |

What remains is helper-call overhead, about nine calls per `pan` element
(FTST after every operation, two FPCR writes for GCC's truncating
conversion, FSMOVE). Removing it would mean emitting host FP operations
inline in `translate.c`; not started. With `pan` at 4.7x, hard float would
free about 0.62 × (1 − 1/4.7) ≈ 49% of Doom's vCPU. Nothing in the guest
executes FPU instructions until phase 3, so the patch changes no guest
behaviour today.

**Phase 1: kernel FPU context.** Userspace stays soft-float.

**Phase 1 result (2026-10-03, implemented).**

- `KernelFpuContext` (`thread.h`): `control[3]` (FPCR, FPSR, FPIAR),
  `data[8][12]`, a 100-byte frame; 208 bytes, at offset 80 of
  `KernelThread` and again as `signal_saved_fpu`. The record is 792 bytes
  and still one page.
- The switch is in `_kernel_restore_user_context` before the latency exit;
  `kernel_fpu_flush` and `kernel_fpu_invalidate` are assembly in
  `vectors.S` and mask interrupts themselves. Release, fork, signal
  delivery and return, thread creation (FPCR inherited) and exec (fresh
  state) follow 2.3-2.4. `kernel_thread_pool_init` clears the owner.
- **Control registers move one FMOVE at a time.** QEMU stores a multiple
  `fmovem.l %fpcr/%fpsr/%fpiar` as FPIAR, FPSR, FPCR from the lowest
  address (`translate.c` `gen_op_fmove_fcr`). I believe Motorola's order
  puts FPCR first, but that is not yet checked against M68040UM/PRM. Since
  save and load were symmetric, every gate passed except the one that
  writes a field: the signal handler started with the interrupted FPCR.
  Before userspace `fenv` code relies on FMOVEM.L order, check the manual
  and fix QEMU if it differs.
- `test-fpu.py` passes. Perturbations seen failing: switch compiled out
  (threads, processes, fork, signals, fresh); fork flush removed (fork);
  signal FPU restore removed (signals, and fork -- the parent takes
  SIGCHLD in `waitpid`). The "fresh state left as a null frame"
  perturbation cannot fail under QEMU, whose FRESTORE ignores its operand;
  the registers come from the FMOVEM of the zeroed record either way.
- Host tests: `test_thread` (owner tracking, flush, invalidate, release)
  and `test_process` (fork copies live state; delivery saves live state,
  clears FPCR/FPSR; return restores it exactly).
- `tools/check_kernel_float_free.py` runs at every kernel link and fails on
  any coprocessor-1 instruction outside the restore path and
  `kernel_fpu_flush`.
- Not done: the `KERNEL_AUDIT` live-owner check, the interrupt-load
  subtest, and the switch-cost measurement.

- Land `test-fpu.py` first and see it fail on today's kernel.
- Implement section 2 and the host tests (5.1). Make the gate pass and run the
  perturbations.
- Measure the IPC round-trip cost of the switch (`~/ipc-prof.sh`) and record
  it in `CURRENT_STATE.md`.
- Add the kernel float-free check.

This phase is safe to ship on its own: nothing else uses the FPU yet.

**Phase 2: toolchain.**

- Apply 4.3 items 1-4 and 6 on a copy of the prefix.
- Test whether `ld` merges `Tag_GNU_M68K_ABI_FP` from shared libraries.
- Extend `test-gcc-driver.sh`.
- Add the QEMU F-line overlay with gate subtest 7.

**Phase 2 result (2026-10-03).**

- `gcc-16.2.0-astra.patch`: `DRIVER_SELF_SPECS` is `-ffixed-a4`, so
  `--with-cpu=68040` selects the FPU and defines `__HAVE_68881__`; an
  explicit `-msoft-float` still wins. `m68k_astra_file_end` emits
  `.gnu_attribute 4, 1|2` in every object. `sin`/`cos` patterns require
  `!TARGET_68040`; `fintrz`, `fscale` and `fmovecr` were already off under
  68040 tuning.
- picolibc: the cross file no longer names a float ABI (the driver's is
  used; `mk/test-picolibc-target.sh` asserts it); `setjmp`/`longjmp` save
  FP2-FP7 under `__HAVE_68881__` (`REG(fp2)`: `m68kasm.h` has no FP names,
  and bare `fp` is A6).
- Built by `~/astra-mg/build-hardfp.sh` into `~/astra-toolchain/prefix-hardfp`
  from a fresh patched tree (`~/astra-toolchain/src-hardfp/work`): gcc,
  libgcc, picolibc, libstdc++. The live prefix is unchanged.
- **The linker merges the tag from shared libraries.** A soft object linked
  against a hard `.so` fails: `libh.so uses hard float, gsp.o uses soft
  float`. So a stale soft library or program cannot link against the new
  libraries at all; the major bumps (4.4) cover only what is already linked.
- The hard-built libc, libm and libstdc++ still call libgcc's 64-bit and
  extended conversions (`__floatdidf`, `__fixxfsi`, `__fixdfdi`, ...): GCC
  uses libcalls for those on the 68040 regardless of float ABI, and libgcc
  is itself built hard. No `__mulsf3`-class helper is referenced.
  `-ffast-math` `sin` is a tail call to `sin`.
- `toolchain/test-gcc-driver.sh` asserts `__HAVE_68881__`, `fsmul`, the tags
  on objects and shared libraries, the refused mixed link, and no
  `fsin`/`fcos` under `-ffast-math`. It passes on `prefix-hardfp` and fails
  on the live soft toolchain, as it must until phase 3 installs.
- `qemu-9.2/target-m68k-68040-fpu-unimplemented.patch`: F-line for every
  FPU opmode outside the 68040's silicon set and for FMOVECR. `test-fpu.py`
  `contract` now requires vector 11; under the previous QEMU it fails
  (`fsin executed in user mode`). `test-host-float.sh` still passes.

**Phase 3: flag day.**

- Flip `mk/m68k-cross.mk:32`, `ndk/make/astra-native.mk:9`, `ndk/Makefile:11`
  and the picolibc cross file.
- Bump the library majors per 4.4.
- Rebuild per 4.5.
- Run the full gate set (`~/astra-mg/gates.sh`), Chocolate Doom and the SDL
  audio and mixer gates.
- Record DE25 Doom host time before and after.

**Phase 3 result (2026-10-03).**

- The hard-float prefix replaced the live one (the soft prefix is
  `~/astra-toolchain/backup-20261003-softfp`); GCC relocated cleanly.
- `mk/m68k-cross.mk`, `ndk/Makefile` and `ndk/make/astra-native.mk` no longer
  name a float ABI. The kernel, the boot firmware and the bare-metal host
  benchmark keep `-msoft-float`.
- Majors bumped (soname, Kit ABI, identity): compiler.library 1 -> 2,
  libc.library 2 -> 3, cxx.library 1 -> 2, lua.library 5 -> 6 (version
  5.5.1 kept), SDL2.library 2 -> 3 and SDL2_mixer.library 2 -> 3 (upstream
  versions kept). Kit manifests and Kit paths follow.
- **The shared runtime archives had to be rebuilt by hand.**
  `libgcc_builtins_shared.a`, `libgcc_unwind_shared.a` and
  `libstdc++_shared.a` come from `tools/build-libgcc-shared-archives.sh` and
  `tools/build-libstdcxx-shared-archive.sh`, not from GCC's install. The
  copied prefix carried the soft-float ones, untagged, so everything linked;
  `compiler.library`'s `__floatdidf` returned in D0/D1 and Lua printed
  `1/2` as `1.0`. Only the terminal gate's Lua check and SDL testtimer saw
  it; Doom and the rest passed.
- Both user linker scripts discarded `.gnu.attributes`, so no shipped image
  carried the tag; they keep it now, and the libc contract requires
  `Tag_GNU_M68K_ABI_FP: hard float`.
- `verify-then-publish.sh` (kernel and userspace tests, every gate, desktop,
  service policy, power, remote desktop, filesystem stress, mailbox,
  providers, NDK, SDL, display, ext4) passes from fresh build trees.

**Board result (2026-10-04, release `2764d8f6`).** `/data/ab-run.sh`, two
runs each, Doom with four TestDraw2 windows, 30 s windows:

| Release | Frames (gen) | Audio gaps |
|---|---:|---:|
| `635131e4` soft float, fast helpers | 507, 515 | 540, 545 |
| `2764d8f6` hard float, host-float QEMU | 646, 631 | 0, 0 |

Frames +25%; the audio gaps are gone. No supervisor faults. The first
hard-float release (`e7f15c72`) exposed a latent uninitialised read in
`astra_vfs_union_directory_open`/`seed_store` that killed the supervisor
on Doom's first launch; fixed in `bf1d9c97`.

**Phase 4 (optional, PLANNED).** Null-frame fidelity in QEMU plus null-frame
laziness in the kernel (2.2), only if phase 1 shows the switch cost matters.
SIGFPE and FPU registers in the fault report and debugger.

## 7. Risks

- **The speedup is smaller than the instruction profile suggests.** It
  measured 2.7x per FPU operation under QEMU user mode on x86, and 2.7x
  for the panning loop on the DE25 against the fast helpers. If the DE25
  number is similar, Doom audio improves about 2x, not 4x. Phase 0 decides
  whether the flag day is worth it against the faster soft helpers.
- **Mixed-ABI binaries fail silently.** Only float and double returns break,
  and only at run time, as wrong values rather than crashes. ABI tags, the ELF
  scan and the library major bumps are the defence. A stale storage volume is
  the likeliest carrier.
- **`setjmp` loses FP2-FP7.** Picolibc's default m68k `setjmp` (4.3 item 6)
  corrupts callee-saved FP registers across `longjmp`. Lua is first in line.
  The symptom would read as a Lua arithmetic bug.
- **A stale FPU owner corrupts freed records** (2.4 rule 1). It surfaces far
  from the cause, as allocator or record corruption. This is why the
  `KERNEL_AUDIT` live-owner check exists.
- **Leaks across processes through fresh state** if anything relies on
  FRESTORE of a null frame under QEMU (2.3). Gate subtest 6.
- **Programs that pass under QEMU but are not MC68040-correct**: transcendental
  instructions, FMOVECR, denormals. The F-line overlay closes the first two.
  Denormals stay an accepted divergence.
- **Interrupt latency.** The switch adds masked time on every owner change.
  It is measured by placing the switch before `kernel_irqoff_latency_exit`.
- **The toolchain rebuild on `beast` is shared state.** It needs the build
  lock and a restorable backup. A partially installed prefix breaks every
  build.

## 8. Document changes required

- `KERNEL_ARCHITECTURE.md:20-21`: split the LOCKED item:
  - "no kernel floating point; the kernel is built `-msoft-float` and executes
    FPU instructions only to save and restore user FPU state";
  - "userspace uses the MC68040 FPU, hard-float ABI".

  This is a LOCKED change, so it needs document review. Also update the
  `KernelThread` size at `:202`.
- `MEMORY_BUDGET.md:415,518`: the thread record is one 4 KiB frame, now with
  416 B of FPU state.
- Replace the soft-float ABI text in:
  - `USERSPACE_RUNTIME.md:168`;
  - `OS_VISION.md:210`;
  - `USERSPACE_BUDGET.md:20`;
  - `ASTRA_VENDOR.md:28-35`.
- Resolve the decisions in `CHOCOLATE_DOOM_PORT.md:75-77` and
  `DEVILUTIONX_PORT.md:25`, and point them here.
- `COMPILER_RUNTIME.md`: the new library majors and what `compiler.library`
  still exports.
- CLAUDE.md, traps: FSAVE/FRESTORE are stubs in QEMU; there is a setjmp FP
  register trap; and "a soft-float object links silently" until the attribute
  lands.

## 9. Decisions needed

1. **Is it worth it?** Proceed past phase 0 only if the DE25 measurement beats
   the patched soft-float helpers by a margin the owner names.
2. **Unimplemented instructions.** Recommended: compile-time avoidance plus the
   QEMU F-line overlay, no FPSP. The denormal divergence becomes LOCKED.
3. **Library versions.** Bump the majors of FP-returning libraries
   (recommended), or rely on the flag day alone.
4. **FPU traps and SIGFPE.** Recommended: FPCR enables stay unsupported and
   `feenableexcept` fails. Alternatively, design SIGFPE now.
5. **The signal handler's FP environment.** Recommended: default FPCR on
   entry. Alternatively, inherit the interrupted FPCR.
6. **Phase 4 null-frame laziness.** Conditional on phase 1 numbers.
