# Handover 2026-10-09: filesystem Phase 1 (Task 5) and boot splash (Task 8)

This supersedes `HANDOVER_2026-10-08_FS_SPLASH.md`. Both lines of work are
still on their own branches and worktrees, and they share beast's single
build lock. **Nothing is pushed.** Local `main` is ahead of `origin/main`.

The authoritative record is the ledger `.superpowers/sdd/progress.md` in the
main checkout `~/Git/astra68`. It is gitignored and Mac-only, and holds every
user decision and deferred finding. Each task has a brief, report and review
file in `.superpowers/sdd/`: `fs1-task-N-*`, `splash-task-N-*`.

## Where things are

| Branch | Worktree (Mac) | Beast tree | Committed tip | State |
|---|---|---|---|---|
| `main` | `../astra68-fix` | — | this handover | — |
| `fs-phase1` | `~/Git/astra68` | `~/Git/astra68` | `9d7a5cbd` | Task 5 re-review fix **in progress, uncommitted** |
| `boot-splash` | `~/Git/astra68-splash` | `~/Git/astra68-splash` (via `~/astra-mg/splash-env`) | `6840c6d6` | Task 8 **in progress, uncommitted** |

**Agents running at handover.** Both may be cut off by the usage limit. If
so, their work is uncommitted in the worktrees listed above. Inspect it with
`git diff`, then finish or discard it. Do not assume it is complete.

1. **FS Task 5, K1 fix.** It is editing `handle.c`, `process.c`,
   `syscall.h`, `hostfs/main.c`, `fssem.c`, `test-fs-semantics.py`, the
   kernel tests, `ABI.md` and the plan. Its last beast run was
   `/tmp/t5/q4/` (`raw-orphans` cases). The report goes to
   `fs1-task-5-report.md`, section "Re-review fix".
2. **Splash Task 8: verify, board publish, captures, docs.** It has
   uncommitted edits to `docs/DEBUGGING.md` and `sw/boot/assets/README.md`.
   The report goes to `splash-task-8-report.md`. It may already have
   **published a release to the DE25**: check `readlink
   /var/lib/astra/current` on the board and `systemctl is-active
   astra.service`.

## Filesystem Phase 1

Plan: `docs/superpowers/plans/2026-10-07-filesystem-phase1.md`. Spec:
`docs/FILESYSTEM_PRODUCTION_PRD.md`.

| Task | State | Commits |
|---|---|---|
| 1–3b | done | see the previous handover |
| 4 rooted sessions, record 3 | **done**, re-review APPROVED | `aa719bd3`, `189c585f` |
| main merged | — | `a338890a` |
| 5 grants are the control plane | implemented; second review **CHANGES REQUIRED (K1)**; fix in progress | `9de5628e`, `cf0c2ea9`, `9d7a5cbd` |
| 6–13 | not started | — |

### Task 5: what is open

- **K1 (Critical) in `9d7a5cbd`.** The new kernel send-disposition mask
  refuses only a zero mask. A nonzero mask that shares no bit with the
  handle's rights, or a TRANSFER-only mask on a non-delegable scope, queues a
  rights-0 entry. The transfer-pool audit then fails at the next launch or
  process-open, and **the release kernel panics**. Any process can trigger
  it. The required fix and its tests are in `fs1-task-5-rereview.md`:
  - refuse a mask that leaves no rights, or only TRANSFER, after the import
    filter, before anything moves;
  - roll back the PORT_CALL reply path with `INVALID_ARGUMENT`;
  - tests on both paths.
- **Minors in the same pass:** M1 reply-mask test, M2 syscall atomicity
  tests, M4 `retire_orphans` before the pump plus a death-path test, M5 the
  plan's Task 11 text (drop the grant ceiling, runtime 1.16), M6 `ABI.md`
  bit-31 flag plus QUERY_ABI value, N1–N4.
- **When the fix lands:**
  1. Run the controller check: `git show <commits> | grep -nE
     '^\+.*(false &&|true \?|scopes\[0\]\.scope, &scope)'` must be empty,
     and no `RESOLVE_` code line may change.
  2. Confirm t9 started after the last restore.
  3. Confirm PROBE PASS.
  4. Send the fix for an opus re-review.
  5. Then Task 6.

### Decisions made 2026-10-09 (binding; also in the ledger)

- **I2, controller decision:** HostFS's grant table grows on demand. The
  `grants 32` setting and `HOSTFS_GRANTS_MAX` are deleted, under the
  standing "no ceilings compiled into an image" rule.
- **I1, controller decision:** a derived grant is owned by the session that
  derived it. It is retired, with everything derived from it, on BYE, on
  death, or when its parent retires. A per-holder quota belongs to Task 11.
- **C1, user decision: a kernel rights mask on handle transfer.** This
  follows Zircon's `zx_channel_write_etc` / `zx_handle_disposition`; Haiku
  has no handle capabilities. It is `ASTRA_PORT_SEND_DISPOSITIONS`, bit 31
  of data[5], with `{handle, rights}` pairs and a narrow-only mask. HostFS
  sends a port derived from a non-delegable grant without TRANSFER. The
  holder binding from `cf0c2ea9` was removed.

### Version table changes (reconcile at merge)

| Item | fs-phase1 | boot-splash | Note |
|---|---|---|---|
| Syscall ABI (`ASTRA_SYSCALL_ABI_VERSION`) | `0x00010042` | `0x00010041` | fs skipped 41 on purpose. At merge, take 42 and keep both features. |
| runtime.library | 1.15 (`astra_port_send_rights`) | — | Task 11 moves to 1.16 |
| Channel config | 3 (Task 4 provider flag) | — | Task 11 takes 4 |
| Storage protocol / Kit | 29 / 7.0 | — | as planned |
| Process VFS exec state | 7 | — | unplanned bump, keeps `features` across exec |

### Carried into later tasks

- **Task 6:** follow symlinks per call, check the opened fd beneath the
  session root, keep `RESOLVE_BENEATH`.
- **Task 8:** a fork child cannot derive a grant yet (no port session).
- **Task 10:**
  - the lane treats PEER_DEAD as close-and-reopen;
  - N6: a thread that keeps cycling host clients exhausts the channel's 8
    scope slots, because forget_scope does not unbind in the kernel.
- **Task 11:**
  - the kernel scope pool is a fixed 64-entry shared partition, against the
    standing instruction. Make it grow, or bound it per holder;
  - per-holder derive quota;
  - runtime 1.16, config v4.
- **Task 13:** medians of three or more runs. fsbench showed one-off budget
  flags that did not repeat (held-miss, seq-write p95, readdir).

## Boot splash (DE25 only)

Plan: `docs/superpowers/plans/2026-10-08-boot-splash.md` on `boot-splash`.
Design: `.superpowers/sdd/boot-splash-design.md`, plus its **Amendment
2026-10-09**.

| Task | State | Commits |
|---|---|---|
| 0–2 | done | see the previous handover |
| 3 boot commands syscall | done, APPROVED | `e63237ad`, `d204a10c` |
| 4 ROM record and POST count | done, APPROVED | `79bef569`, `d9b39225` |
| 5 supervisor launch list and gate | done, APPROVED | `7e223dd7`, `da4010e2` |
| 6 helper draws splash and bar | done, review fixes applied | `cbf1187f`, `aeb00f8b`, `efebeb8a` |
| 7 release ships the splash | done, controller-reviewed | `6840c6d6` |
| 8 verify, board, docs | **in progress** | — |

**User decision, 2026-10-09: failure is tier-aware.**
- A `required` or `critical` service that fails to start makes the record
  FAILED, and the text console shows.
- An unmarked (user-tier) service that fails is reported SKIPPED. The bar
  completes, and the failure appears only in the log and on serial.
- Haiku precedent: launch_daemon failures never touch the boot splash.
- Under host QEMU, media and remote-desktop are always SKIPPED: there is no
  audio host and no broker.

**Task 8's brief has binding controller additions** (`splash-task-8-brief.md`):
- the docs must describe tier-aware failure;
- board checks M2 (a helper restart after the handoff draws over the
  desktop until the next present) and M4 (the captures prove the helper's
  `main` wiring);
- measure the 4 MB copy per step;
- restore the board with `systemctl restart astra.service` after the
  FAILED-write perturbation.

**After Task 8:** run a whole-branch review of `b6c02739..HEAD`. Then
merge to main with a full `verify-then-publish`, not only t9 (the lesson of
the system-log merge), and reconcile the ABI with fs-phase1.

### Facts established

- The ROM budget is **512 KiB** (`ASTRA_ROM_SIZE 0x80000`), not 256 KiB.
  `astra_boot.bin` is 412,524 bytes after Task 4.
- **The bare ROM probe ends FAILED by design.** It samples after handoff,
  and without `ASTRA_HOSTFS_ROOT` hostfs exits with status 9 (no
  HOST_DEVICE). Probes for later gates must set `ASTRA_HOSTFS_ROOT`.
- **`run-arty.sh`'s old `process_alive` hung only under bash** (a bare
  `return` in the EXIT trap gave 143). The DE25's `/bin/sh` is dash, so the
  board was not affected. Fixed in `cbf1187f` and `efebeb8a`.
- **verify-then-publish greps for "FAIL"**, so no PASS marker may contain
  that substring.
- **`gates.sh` on beast now runs `test-boot-progress.py`**, but only in
  trees that have it. The backup is `~/astra-mg/gates.sh.pre-splash5`, and
  the default (fs tree) dry run is identical to before.
- `/tmp/splash/t7-release` on beast is a read-only release from Task 7.
  Remove it with `chmod -R u+w` first, then `rm -rf`.

## Other open items

- **#11 testtimer.** The known intermittent ("Delta time erratic"). It
  failed once in the Task 5 C1 t9 and passed 3 of 3 reruns. It still has no
  root cause.
- **#12:** two pre-existing gate defects: hostbench's command total, and
  `--interface-layout-only`.
- **DE25 board:** an ext4 warning on `mmcblk0p2` (needs an offline
  e2fsck), `sr_mod` needs a kernel rebuild (Phase 4), and the USB hub is
  degraded.
- **Process lesson:** agents sharing one worktree, or one beast tree, must
  sync under the lock. On 2026-10-09 an unlocked partial sync by one agent
  disturbed another's publish run. The publisher's source check caught it.

## Resume checklist

1. Read the ledger tail, then `git log` and `git status` in both worktrees.
2. Read any `.superpowers/sdd/*-report.md` or `*-review.md` newer than this
   file.
3. On beast: `cat /tmp/astra-build.owner`, and check for stray
   `run-arty.sh` or QEMU processes.
4. **FS:**
   1. If the K1 fix is committed, run the controller check, then an opus
      re-review.
   2. If it is uncommitted, review the diff and finish it: kernel tests,
      t9, probe.
   3. Then Task 6.
5. **Splash:**
   1. Check the board state (release id, `astra.service` active).
   2. Finish or verify Task 8 against its brief, including the docs commit.
   3. Then the whole-branch review and the merge.
6. Push nothing without the user's word.
