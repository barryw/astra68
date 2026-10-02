# Handover 2026-10-02: process spawn, shared page tables

Read `CLAUDE.md`, `AGENTS.md`, then this page. Previous:
`HANDOVER_2026-10-02_REPEATED_WORK.md` (its "workload suite" section is
the measurement method used here).

## Rule: judge every change on the workload suite

`emu/qemu/bench-workloads.py` runs six fixed amounts of unrelated work
under the profiling QEMU, one interval each; `tools/astra-prof workloads`
compares profiles (kernel and guest instructions per workload). Two runs of
one kernel agree within ~1%. Ad hoc work: `--shell NAME=COMMAND --only
NAME`; callers of a kernel function: `--probe ADDR` (address from
`m68k-astra-nm` on the kernel ELF), then `astra-prof report --label NAME`.

## Result (base `3f82836d` -> area spans)

| workload | guest before | guest after | kernel before | kernel after |
|---|---:|---:|---:|---:|
| boot | 202.7 M | 191.4 M | 97.4 M | 85.6 M |
| spawn (20 x `ls` from zsh) | 216.8 M | 139.4 M (-36%) | 163.1 M | 86.3 M (-47%) |
| fs (fsstress, 4 forked workers) | 62.4 M | 46.4 M | 42.3 M | 26.4 M (-38%) |
| heap | 32.7 M | 27.1 M | 18.0 M | 12.5 M |
| lua | 56.9 M | 48.0 M | 33.9 M | 25.0 M |
| doom-start | 123.9 M | 109.6 M | 63.6 M | 49.3 M (-22%) |

Not yet measured on the board.

## What changed (in order)

1. `edac85f1` wait queues link by pointer.
2. `80eaa123` pages about to be overwritten are not filled first
   (`kernel_memory_alloc_*unfilled_tagged`; audit builds still poison).
3. `a8e2416d` **execve streams through the launch transaction**:
   `PROCESS_LOAD_REPLACE` (103) builds a replacement address space from
   the ranges the kernel asks for; `PROCESS_EXEC` (66) commits that load.
   The whole-image exec and `astra_process_exec` are gone. ABI 0x0001003B.
4. `a2688b3b` **library code through shared page tables**: one MC68040
   page table per 256 KiB span of immutable library pages, built by the
   library cache, pointed at by one descriptor (`VM_DESC_SHARED_TABLE`,
   bit 4 of a pointer descriptor) in every process. The library link
   puts writable data on its own span. See
   `docs/SHARED_LIBRARY_FORMAT.md` "Shared code spans".
5. (this commit) **areas through shared page tables**: an area mapping is
   whole spans; each span gets a table at its first committed page and
   that table is attached to every mapping at once; commit/decommit is
   one descriptor. Read-only mappers get write protection on the pointer
   descriptor (QEMU and the 68040 honour WP at every level; `probe_root`
   now does too). Area mappings now occupy whole spans: at most 512 area
   mappings per process (128 MiB window).
6. `06b30f8a` (found gating 5, pre-existing) **reaper URP race**: an
   interrupt inside `kernel_vm_deactivate`'s compare-then-switch left a
   woken thread current on the empty root; the supervisor faulted on its
   own code about one boot in 500. Deactivation now runs with interrupts
   off. Repro harness: many parallel `test-terminal.py --prepared-image
   --restart-menu-only` runs with the ROM refresh stubbed out.

## How the cost was found

Per command, fork+exec of `echo` cost 8.9 M; ~1,100 per-page VM
operations at ~5 k instructions each were most of it. Probing callers
showed the bulk was **shared areas** (VFS transport and file-handle
areas, ~170 pages mapped and unmapped per process), not library code.

## Next, largest first

1. ~~Loader symbol resolution~~ -- DONE with GNU hash (loader 22.3 M ->
   13.0 M instructions per 20 launches, spawn -6.6%). What is left, ~0.5 M
   per launch: hashing each name (~170 instructions), the version-string
   compare, ~2,000 RELATIVE relocations and the verneed walk. The next
   lever is caching a library's relocated data when the program does not
   interpose; it removes nearly all of it.
2. **Storage reads per launch** (~0.9 M): the loader and the program are
   read through the VFS every launch. Cache the interpreter in the
   library cache (it is a library) and map it like one.
3. **fork per-page work**: zsh's private pages still clone page by page
   (`kernel_vm_clone_address_space`). Lazy fork (child faults pages in)
   only if the suite still shows it after 1-2.
4. **Scheduler** (task agreed with the user): Haiku-style penalties for
   CPU-bound threads (`~/Git/haiku/src/system/kernel/scheduler`),
   kernel work never starved, priority donation over `PORT_CALL`, plus a
   `nice`/`renice` command (`PROCESS_PRIORITY` exists). Needs a mixed-load
   workload in the suite first (CPU hog alongside Doom; frame rate, audio
   underruns, wake latency).

Found, not fixed: no `/dev/null`; `fork()` returns ENOTSUP while the
process has any regular file open (`astra_posix_file_fork_ready`).
