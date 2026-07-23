# Astra 68 kernel status

Status date: 2026-07-22

This is the kernel-specific truth table. `CURRENT_STATE.md` remains the whole
machine continuation map. A row marked CURRENT has evidence; PLANNED or MISSING
must not be presented as working software.

## Current source identity

- Branch: `main`.
- K1 functional source commit: `66d6094f9339469313fefb70b259d07a7c2272ce`.
- K1 lifecycle-soak source commit:
  `470bf123cf24bbadf3525f91307e3d9aebe92006`.
- Immutable build snapshot: Beast `/tmp/astra68-k1-66d6094` from Git archive
  SHA-256 `ba4d91999cf829c33a345d895b7966a438b28b93871d8d34843d658a1d0c0039`.
- Immutable soak snapshot: NUC `/tmp/astra68-k1-soak-470bf12` and independent
  Beast `/tmp/astra68-k1-soak-beast` from Git archive SHA-256
  `b5db0e133ee04605fc1e18e4a159e1893893ca5c90c54df1c2ad8bcfc0c64fa5`.
- The qualified normal, direct-panic, and guard-panic ROMs report that complete
  Git identity and reproducible commit timestamp.

## Works now

| Mechanism | State | Evidence |
|---|---|---|
| BootInfo validation and separate kernel image | CURRENT | host, Musashi, full RTL |
| kernel VBR and 8 KiB supervisor stack | CURRENT | Musashi, full RTL |
| frame allocator for all 8,192 4 KiB frames | CURRENT | host tests, target startup |
| SRP/CRP 4 KiB two-level translation | CURRENT | host, focused RTL, Musashi, full RTL |
| PMMU enable and CRP switching | CURRENT | Musashi and full RTL K1 |
| user null/code/stack guards | CURRENT | host mapping tests and target fault |
| supervisor stack guard | CURRENT SIM | exact descriptor host test plus deliberate full-RTL format-A guard panic |
| cross-CRP cache isolation | CURRENT SIM | distinct same-address code and stack markers on Musashi/full RTL |
| duplicate cached user-alias rejection | CURRENT HOST | one-bit/frame VM ledger and remap test |
| whole-address-space cache invalidation | CURRENT HOST | destruction invalidates before descriptor removal/frame reuse |
| format 0/1/2/9/A/B frame decode | CURRENT | byte-exact host tests |
| SFC/DFC copyin/copyout fault recovery | CURRENT | focused RTL, Musashi, full RTL |
| typed generation handle table | CURRENT | host tests; 16 entries/process in K1 |
| process creation and owner teardown | CURRENT | host, Musashi, full RTL |
| deferred pinned-DMA reap | CURRENT HOST | bounded safe-point/idle path tested on host |
| two isolated user processes | CURRENT SIM | same ROM on Musashi and full RTL |
| 100 Hz preemptive round-robin | CURRENT SIM | 3 RTL and 3 Musashi switches before K1 marker |
| trap ABI query/progress/yield/exit/close | CURRENT PROVISIONAL | host and target K1 |
| offender-only user fault death | CURRENT SIM | format-B fault reaches `K1OK` |
| last-process supervisor idle transition | CURRENT HOST | process/dispatch tests; target assembly builds |
| panic to console and retained early log | CURRENT SIM | full-SoC deliberate panic test |
| K1 host analyzer/sanitizer gates | CURRENT | 11 suites, analyzer, ASan/UBSan |
| deterministic lifecycle-soak harness | CURRENT SIM PARTIAL | exact four-cycle full RTL and 100-cycle Musashi pass; independent 500,000-cycle runs beyond 110,000 and 100,000 |
| shared CPU/PMMU framework | CURRENT | 90 tests, 30 adapter executions, Harte smoke |
| CACR independent I/D commands | CURRENT RTL | Motorola-directed mixed CI/CD decoder test; strict inventory 139/113 clean |

## Hardware status

- The ULX3S attached to NUC runs the older persistent `6C0D0CA3` K0 release.
- Routed SRAM candidate `F4DC1E18` proves the repaired PMMU core and K0 platform,
  not the staged K1 kernel.
- K-HW3 table-walk arbitration, K-HW4 timer/IACK changes, and K1 have now been
  synthesized and placed together, but not fully routed, flashed, or soaked on
  hardware.
- Exact build `66D6094F` has a zero-SCC complete synthesis and finished
  placement on Beast. Its strict seed-4 router1 job is still running; neither
  placement estimates nor an active route are acceptance evidence.
- Therefore K1 is not a hardware-qualified kernel and is not production-ready.

## Required before K1 release

| Requirement | State |
|---|---|
| move resource destruction out of hard IRQ | CURRENT, host revalidated |
| supervisor stack guard in SRP | CURRENT SIM, hardware remains |
| exact cache synchronization/alias test for loaded user code | CURRENT SIM, hardware remains |
| committed nonzero ROM/Git identity | CURRENT SIM |
| full normal/direct-panic/guard-panic RTL rerun | CURRENT from `66d6094f` |
| exact 12.5 MHz CPU / 60 MHz SDRAM complete route | MISSING; exact job active |
| repeated ULX3S POST, SDRAM, PMMU, timer, fault, HDMI | MISSING |
| long context/syscall/fault/allocation soak | CURRENT SIM PARTIAL; release-duration jobs remain |
| panic HDMI and retained-log check on physical board | MISSING |

## Partial or transitional K1 code

- `KernelProcess` combines one process and one thread; stable code must split
  them before waits, IPC, or multi-threaded processes.
- Four process slots and 16 handles/process are qualification limits, not the
  stable limits in `KERNEL_ARCHITECTURE.md`.
- The scheduler is round-robin only. Priority queues, bitmap selection,
  inheritance, donation, wakeup boost, and real-time budgets are not built.
- `block.c` and `dma.c` qualify ownership, generation, pin, completion, and
  revocation. Filesystem/block policy still belongs in a user service.
- Teardown drains at syscall safe points and supervisor idle because no kernel
  worker thread exists yet. Hard IRQ does not perform teardown.
- The diagnostic UART mirror is emergency FTDI output only. ESP runtime
  communication is SPI.

## Not implemented

- boot allocator retirement, typed slabs/object caches, bounded general heap,
  emergency page reserve, per-subsystem allocation tags, and low-memory policy;
- separate thread objects and kernel stacks, stack canaries/high-water, and
  final ISP/MSP strategy;
- shared areas, demand-zero pages, stable-default 8 KiB page option, commit accounts, and
  cache-safe executable/file-page reclamation;
- ports, messages, handle transfer, wait-multiple, semaphores/events, deadlines,
  cancellation, priority inheritance, and priority donation;
- IRQ endpoint allocation, deferred worker threads, service restart, and
  privileged device-mapping objects;
- centralized typed MMIO accessors and complete bus-timeout classification;
- fixed allocation-free trace ring and interactive SPI/FTDI monitor commands;
- graphics-service command ring/mapping revocation and dedicated graphics RAM
  arena;
- full deterministic allocation failure injection and release soak thresholds.

## Known invariants and tests

| Invariant | Test state |
|---|---|
| user cannot map kernel/page-table frames | host VM tests |
| null and W+X mappings rejected | host VM tests |
| stale handles cannot reach reused slot | host handle tests |
| killed owner releases mappings/handles/frames | host process test, target K1 |
| pinned DMA delays but does not lose teardown | host process/DMA/block tests |
| user-copy fault returns error, unrelated kernel fault panics | host and full target |
| nested timer on supervisor exception resumes handler | host dispatch and focused RTL |
| fault after repeated traps stacks on ISP | focused Questa wait/zero-wait |
| hard timer path performs no maintenance/destruction | host dispatch test |
| kernel stack guard descriptor is invalid and adjacent pages remain mapped | host VM test |
| kernel stack guard access reaches retained panic path | full-RTL exact-address diagnostic; hardware missing |
| duplicate user cached alias is rejected and reusable after unmap | host VM test |
| cross-CRP same-address code/data cannot expose stale bytes | Musashi and full RTL K1 |
| malformed syscall corpus never panics | MISSING |
| every allocation site unwinds exactly | MISSING |

## Next actions

1. complete and validate the exact production route on Beast;
2. finish the exact RTL/Musashi lifecycle runs;
3. flash and qualify through NUC, then run the hardware soak;
4. implement and benchmark 8 KiB against the retained 4 KiB oracle before
   freezing the stable VM ABI.
