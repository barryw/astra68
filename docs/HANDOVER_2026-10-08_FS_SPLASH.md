# Handover 2026-10-08: filesystem Phase 1 and boot splash

Two lines of work are in flight on separate branches and worktrees. They share
beast's single build lock. Nothing is pushed: local `main` is 68 commits
ahead of `origin/main`.

The authoritative task record is the ledger `.superpowers/sdd/progress.md`, in
the main checkout. It is gitignored and lives on the Mac only. Its sections
"Filesystem Phase 1" and "BOOT SPLASH" record every user decision and every
review finding that was deferred to a later task. Read it first.

## Where things are

| Branch | Worktree (Mac) | Beast tree | Tip | State |
|---|---|---|---|---|
| `main` | `../astra68-fix` | — | `aac88daf` | system log merged, regressions fixed; plus this handover |
| `fs-phase1` | `~/Git/astra68` | `~/Git/astra68` | `81e4cf5f` (main merged) | Task 4 review fixes **in progress** |
| `boot-splash` | `~/Git/astra68-splash` | `~/Git/astra68-splash` | `11581bf8` | Task 3 **in progress** |
| `fs-perf-proto` | `~/Git/astra68-fsproto` | — | uncommitted | scoped-lane prototype; reference only |
| `feat/cdfs`, `cdfs-merge-archive` | `/private/tmp/astra68-cdfs` | — | — | archived; CDFS removed from main |

**Agents still running at handover.** Their results arrive as commits on those
branches and as report files in `.superpowers/sdd/`:

- **FS Task 4 review fixes** (`fs1-task-4-report.md`, "Review fix" section):
  - **I1:** make CLAIM kernel-authenticated, checking the host-scope right as syscall 109 does.
  - **M1:** stop an in-flight OPEN from leaving a handle on a closing session. `closing` must be set under `files_lock` before the handle walk.
  - **M2:** close the race between CLAIM_RELEASE and SESSION_OPEN.
  - **M3:** add tests for cross-session RENAME/LINK and for escapes from a derived session.
  - **M4:** copy the path, then check it for NUL.
  - **M5:** document session-id wrap.
  - **Must keep `RESOLVE_NO_SYMLINKS`.** Symlink following belongs to Task 6.
- **Splash Task 3** (`splash-task-3-report.md`): boot commands carried over `ASTRA_SYSCALL_PROGRESS`. The hard requirement: intercept `0x4253xxxx` before `current->progress` is assigned.

**Before accepting any FS commit**, the controller's own check is: `git show <commits> | grep -nE '^\+.*(false &&|scopes\[0\]\.scope, &scope)'` must be empty, and no `RESOLVE_` flag may have been dropped. The implementers keep perturbations live, and the security scanner flags them. The t9 log must postdate the last restore.

## Filesystem Phase 1

- **Plan:** `docs/superpowers/plans/2026-10-07-filesystem-phase1.md`, revised in `bebb3ede` for the scoped lane.
- **Spec:** `docs/FILESYSTEM_PRODUCTION_PRD.md`.

| Task | State | Commits |
|---|---|---|
| 1 DE25 baseline | done | `0d370efb`..`133dc6bc` (evidence: NAS `builds/fs-phase1-baseline-20261007-fa1026ad`) |
| 2 gate infra, root helper | done | `1de2dd26`..`60f2d30a` |
| 3 relay (superseded) | done | `62b72fc2`, `6d4e6a2c` |
| 3b scoped direct lane | done | `0ba9f1e1`, `048f609a` |
| 4 record 3, rooted sessions | review fixes in progress | `aa719bd3` + fix pending |
| 5–13 | not started | — |

### Decisions that bind the remaining work

All are recorded in the ledger with their dates.

- **Linux owns filesystem machinery.** Astra owns the namespace, grants and policy (the PRD).
- **The scoped direct lane (2026-10-08).** The relay through HostFS cost about 2.6x: four cross-address-space switches per op. Instead:
  - HostFS is the control plane. It mints and revokes kernel scopes (syscalls 109/110/111).
  - The kernel binds each client channel to a scope.
  - QEMU enforces root, rights and session on every command.
  - Measured against the old direct path: stat 0.91–1.03x, read 1.00–1.02x, 0 ns of HostFS time per op.
- **Delegation.** A scope carries TRANSFER only if its grant is delegable, and transfer never widens it.
- **Fast path.** Inline-on-vCPU execution (`RESOLVE_CACHED`/`RWF_NOWAIT`) is its own task, 12b, before Task 13.
- **TLB/ASID across switches.** A separate investigation after Phase 1.
- **Performance gate** (`tools/fs-probe-compare.py`):
  - cross-switch delta at most +0.05 for stat and +0.5 for read;
  - HostFS CPU at most 500 ns/op;
  - syscalls at most direct + 0.1;
  - median and p95 within 10%;
  - at least 5 interleaved runs, because run-to-run noise exceeds 10% on several workloads.
- **Beast gates drop privilege.** `tools/astra-qemu-root` loop-mounts a root-owned copy of the image under `/var/tmp/astra-qemu-root.XXXXXX`, then runs QEMU as the caller. Only the sudoers entry for that path exists.
- **Provider loss.** The supervisor restarts the managed services that hold grants; applications get `ESTALE`.
- **Budget.** Provider objects on the DE25 start at 64 MiB.
- **Board.** Publishing to the board is allowed.

### Carried into later tasks

The details are in the ledger.

- **Task 5:**
  - the shared lane keeps the first scope's handle;
  - a system-level two-client fork test;
  - `GRANT_DERIVE` returns a grant port.
- **Task 6:** follow symlinks per call, not globally. Check the opened fd against the session root, and keep `RESOLVE_BENEATH`.
- **Task 10:** the lane treats `PEER_DEAD` as close-and-reopen.
- **Task 13:** compare medians of 3+ runs. Three cheap read-path trims are noted if headroom is needed.

## Boot splash (DE25 only)

- **Design:** `.superpowers/sdd/boot-splash-design.md`.
- **Plan:** `docs/superpowers/plans/2026-10-08-boot-splash.md` on `boot-splash` (`d1f771af`, `512296ee`).
- **The design in brief:**
  - The ARM helper draws the 1080p splash and a thin bar.
  - The fill is fixed phase shares: 30% across 21 fixed steps, then 70% across N services.
  - The text console appears only on failure.
  - The record sits at text page `0xFE0`/`0xFE4`.

| Task | State | Commit |
|---|---|---|
| 0 isolated beast tree | done | beast-only scripts |
| 1 record ABI header | done | `9386e9a7` |
| 2 kernel continues record | done | `11581bf8` |
| 3 boot commands syscall | in progress | — |
| 4–8 | not started | — |

**The beast isolation.**
- `~/astra-mg/tree-env.sh` parameterizes seven scripts; each has a `.pre-splash` backup.
- The splash work runs as `~/astra-mg/splash-env <cmd>`, which sets its own tree, images (`~/astra-mg/splash`), logs (`/tmp/splash/`) and QEMU cache.
- With nothing set, the defaults behave byte-identically for the canonical tree (the dry-run proof is `splash-dryrun.sh`).
- `fix-env` does the same for a third tree.
- **Verification on splash:** use `verify-then-publish.sh`. t9 there calls `fs-gates.sh`, which exists only on fs-phase1.
- **Baseline:** READY-TO-PUBLISH; time-boot puts the terminal up at 0.68 s median.

## Other open items

The task numbers are this session's.

- **#11: testtimer.** An intermittent that prints no reason when it fails. It is the only thing that has blocked READY-TO-PUBLISH, and it needs a root cause.
- **#12: two pre-existing gate defects** that serial-only timeouts used to hide:
  - hostbench's command total ignores other host traffic;
  - `--interface-layout-only` can't pass, because Interface Gallery isn't launched at boot.
- **The DE25 board:**
  - `dmesg` shows an ext4 warning on the root fs (`mmcblk0p2`), which needs an offline `e2fsck`;
  - optical drives (`sr_mod`) need a rebuilt kernel image (`CONFIG_CDROM` changes `struct gendisk`); planned for Phase 4;
  - the USB hub degraded. The physical USB/optical tests need a powered hub, a stick and a DVD drive.
- **Lesson:** verify a merge with `verify-then-publish`, not only t9. The system-log merge broke the NDK headers and a gate, and t9 did not catch it.

## Resume checklist

1. Read the ledger, then `git log` on all three branches.
2. Read any report or review files newer than this handover in `.superpowers/sdd/`, starting with `fs1-task-4-report.md` and `splash-task-3-report.md`.
3. On beast, check `cat /tmp/astra-build.owner` and whether the lock is held, plus any running `~/*.pid` jobs.
4. **FS:** apply the perturbation grep, then dispatch Task 4's re-review (it ran on the strongest model). Merge main into fs-phase1 if main moved. Continue with Task 5.
5. **Splash:** review Task 3, then Tasks 4–8.
6. Push nothing without the user's word.
