# Handover 2026-10-06: system log, phase 1

Branch `system-log` (from `main` at `0c1467c7`), 37 commits, not merged and
not pushed. Design: `docs/SYSTEM_LOG.md`. Plan:
`docs/superpowers/plans/2026-10-05-system-log-phase1.md`. The progress ledger,
with every task's commits and deferred minors, is
`.superpowers/sdd/progress.md` (gitignored, local to the Mac checkout); its
"System log phase 1" section is the authoritative task record.

## What works now

Every program writes its log lines into its own lock-free ring, with no
syscall, lock or wait per line. The events service drains the rings and the
kernel trace ring into one time-ordered text file, `/events/syslog` on the boot
volume, plus a live copy on the host at `WORK:.astra/syslog`. On the DE25 that
copy is `/var/lib/astra/hostfs/work/.astra/syslog`, and under the QEMU gates
it is `<work>/hostfs/.astra/syslog`. Lines name their program, and
`cat EVENTS:log/syslog` reads the file in the guest.

Under QEMU, Chocolate Doom's start-up lines reach the file as
`chocolate-doom[...]`. On the second full run of the final tree all 16 gates in
`~/astra-mg/gates.sh` pass, and so do `test-events.py`, `test-terminal.py`
(111 commands) and `test-desktop-apps.py`.

## Tasks

| Task | State | What |
|---|---|---|
| 1 | done | Time page ABI and `astra_time_now` (`sw/include/astra/time_page.h`) |
| 2 | done | Kernel publishes the time page at `0x48000000` in every process; clock LO/HI reads masked |
| 3 | done | Lock-free per-process ring (`sw/include/astra/log_ring.h`, `runtime/src/log_ring.c`) |
| 4 | done | `astra_log_line`: lazy setup, generation-checked state, fallback to `LOG_WRITE`, fork/exec reset, dead-service latch |
| 5 | done | Line format and rotating file (`events/src/log_format.c`, `log_file.c`) |
| 6 | done | Fair, budgeted, ring-ordered drain and merge (`events/src/log_clients.c`) |
| 7 | done | Events service is the log service: `LOG` endpoint, names from kernel `PROCESS_START`, clock-free restart resume point, restart and loss markers |
| 8 | done | `LOG` granted to every program except its provider; gates read the syslog; 1024-slot rings; drop count published in the ring header |
| 9 | **in progress, uncommitted** | POSIX fd 1/2 as separate log descriptions (stdout notice, stderr warning), per-thread line assembly, flush in `_exit` |
| 10 | not started | `log show` / `log stream` command, `logburst` probe, `test-system-log.py` |
| 11 | not started | Docs (ABI, memory map, DEBUGGING, CURRENT_STATE) and a DE25 board run |
| final | not started | Whole-branch review, then merge |

Task 9's uncommitted edits are in `sw/userspace/posix/` (`console.c`,
`posix_descriptor.h`, `tests/test_descriptor.c`, `tests/test_poll.c`). An agent
was still running its gates when this was written. Check its report at
`.superpowers/sdd/system-log-task-9-report.md` before touching those files.

## Things the plan got wrong, now settled

- **Port messages must begin with `AstraMessageHeader`.** The kernel refuses
  anything else. Setup is `AstraLogSetup` (op SETUP=1, protocol version 2) and
  the doorbell is a header-only message (op DOORBELL=2). A handle moved in a
  message needs `ASTRA_RIGHT_TRANSFER`.
- **The kernel ring object is never notified.** A consumer position reported
  past the producer's kernel shadow fails the ring. The doorbell is a
  non-blocking try-send to the `LOG` port.
- **There is one ring per process, not per thread.** `runtime.library` has no
  TLS, so writers coordinate with CAS. `publish()` advances once per verified
  run of slots, never mid-record.
- **Fork shares mapped areas.** The child resets its log state
  (`astra_log_after_fork_child`, called from `astra_process_clone`). Exec closes
  producer endpoints (`astra_log_before_exec`).
- **A send handle to a dead port made fork fail with status 5.** Fixed in
  `kernel_port_handle_retain`. Every zsh fork failed after any service restart.
- **Program names come from the kernel's `PROCESS_START` trace records.** A
  synchronous PROC lookup stalled the whole drain at boot.
- **Rings are 1024 slots in a 256 KiB area committed on touch.** The 256-slot
  ring lost half of testkeys' 512-line burst, because the service runs below
  application priority.
- **The `LOG` grant needs `delegated` rights.** Without TRANSFER, Terminal
  could not forward it to zsh and zsh never started.

## Open items

- **testtimer** failed once in 33 branch runs, against 0 failures in 31 on
  main. It is not explained. A suspected trigger is the first `SDL_Log`, which
  sets up the ring and produces the first syslog write. If it recurs, capture
  the trace around that call.
- **Minors deferred to the final review** are listed per task in the ledger.
  The notable ones:
  - `save_snapshot` still syncs on every pump.
  - The time page costs 8 KiB of page tables per process; a shared leaf table
    would cut that.
  - A heavily logging process commits about 252 KiB of ring.
  - Name pinning covers only processes started before READY.
- **Task 10 must fold in** stricter testkeys checks: assert all 512 lines and
  no drop marker, keep the fault scan for every pid, and take the pid from the
  first line that carries one.
- **Task 11 must document** log-ring memory in `docs/MEMORY_BUDGET.md`.
- **Phase 2 of the spec** is not started: zlib, gzip rotation, retention. So is
  phase 3: `syslog()`, `SDL_Log` routing, terminal echo, and `open` stream
  passthrough.

## Related work from this session, not on the plan

- **`docs/SCUMMVM_PORT.md`**: a feasibility audit. Porting is practical with
  the `sky`, `queen` and `lure` engines first and *Beneath a Steel Sky* as the
  gate game. The top gaps are app paths (no `HOME`), a MIDI driver over
  `astra_midi_send`, and build plumbing. Estimated at about 2-3 weeks to a
  board run.
- **`docs/ENVIRONMENT.md`**: the approved design for environment variables.
  libc synthesizes `HOME`, `TMPDIR`/`TMP`/`TEMP`, `PATH`, `USER`/`LOGNAME`,
  `SHELL` and per-app XDG directories from assigns. Stored variables are
  per-app knobs only: manifest defaults plus a user override file, merged by
  the supervisor. There is no system layer. The Get Info editor is phase 2. It
  is separate work for its own branch after this one merges.

## Working rules this run relied on

- **Builds and gates run on beast under `~/astra-mg/locked`.** Sync with the
  rsync line in `AGENTS.md`. Build userspace before `sw/boot`.
- **A main-branch build lives at `~/Git/astra68-main` on beast,** with images
  in `~/astra-main-img`. Use it to tell a regression from a pre-existing
  failure.
- **Every fix was test-first with a deliberate perturbation**, and was reviewed
  until clean.
- **Background wait loops of the form `until ssh beast '! pgrep -f X'` never
  end.** The remote shell matches its own command line. Use
  `pgrep -f '[X]'` or a PID file.
