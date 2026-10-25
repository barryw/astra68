# Filesystem continuation, 2026-10-10

Branch: `fs-phase1`; this checkpoint includes the Task 5 review fixes,
Task 6–12 implementation and final correctness/qualification review. Git
history records the committed identity; the exact deployed source manifest
and immutable release are recorded below. The plan is
`docs/superpowers/plans/2026-10-07-filesystem-phase1.md`. The private decision
ledger and reviews remain in `.superpowers/sdd/`.

## Latest checkpoint, 2026-10-11

Final whole-branch review reported four Important findings and no Critical
findings: joined-assign open shadowing, discarded rename/link escape payloads,
missing native directory-relative namespace context, and root callers accepted
by the privileged mount helper. All four fixes are written. Host VFS and
ASan/UBSan suites pass; the full coherent guest/provider rebuild completed
under `fs-phase1-performance-20261011/review-production-2`. The first production
attempt exposed a freestanding `strrchr` dependency, removed by scanning the
existing path buffer; its failed build log is retained in `review-production-1`.
Current-source correctness now passes `review-target-1` and
`review-integration-1` (both EXIT 0): all 26 semantics including real native
and POSIX namespace continuation, quota/restart/recovery/fsck, 1,600 stress
operations, raw HostFS, NDK headers/docs, POSIX host/sanitizers and all 31
integration gates. Terminal passes 111 commands; all six power actions,
desktop applications, remote desktop, provider index and events pass.
`review-de25-provider-1` also passes on physical AArch64 as UID 65534;
DIR growth is 30.08 KiB/object and configured conservative allowance remains
56 MiB within the 64 MiB provider budget.

Current contracts are Filesystem ABI 12.0/Filesystem.kit 9.0.0, STOR 36,
Host 0x00010015/configuration 4, syscall 0x00010043 and runtime 1.16. All ten
native path-based `_at` APIs now take filesystem context; the canonical VFS
continues ordered provider escapes from retained directory handles. QEMU
rename/link returns its existing ordered escape payload. Joined opens use the
member selected by namespace lookup, without falling through after disappearance.
The root helper refuses UID/GID 0 before opening or mounting anything; all
21 helper tests pass with no leaked mounts or directories.

On the preceding ABI 11 source, Tasks 5–12 pass the complete native-16 correctness checkpoint: kernel and
userspace hosts/sanitizers, MC68040/QEMU build, raw provider, all 26 FSSEM cases,
admission/flood/recovery/fsck, bootstrap match/absence/mismatch, NDK contracts
and documentation, and the AArch64 cross-build. DE25 calibration-3 passes the
current provider contract unprivileged. Integration-1 passes all 31 gates,
desktop applications, six power actions, remote desktop, provider index,
all 26 semantics/recovery checks, root-helper 20 checks, 1600-operation stress,
HOST_DEVICE confinement and events. NDK/docs and provider coverage pass. The
runner finishes exit zero with all 164 source hashes unchanged. Tasks 5–12
correctness is qualified; exact source archives, logs and artifact hashes are
under `fs-phase1-task12-20261011/{native-16,integration-1,de25-calibration-3}`
on Beast.

Those checkpoints used Filesystem ABI 11.0/Filesystem.kit 8.0.0, STOR 35,
Host 0x00010015/configuration 4, syscall 0x00010043 and runtime 1.16. Earlier
correctness failures below remain recorded with their fixes and later passing
checkpoints.
Task 12b and Task 13 performance qualification remain open. The selected DE25
release is now `194b9202da7eb0ff61a05ad03633bd63f687ac7afce5fe05c08601fc357af249`,
carrying ABI 12, the worker-only resolver optimization and refreshed icons.
`review-release-1` and `review-deploy-1` both finish EXIT 0: exact staged guest
semantics/recovery/fsck, deployment integrity, all nine live threads' credentials
and physical device bounds pass. QEMU drops to `astra` UID/GID
999 before executable startup; physical device confinement passes. The prior
privilege-corrected baseline is `299a6ade22eedddce3ae4db7f7cf8d15f08efa9e81be394649512ab4aa60f3a8`.
Real desktop and responsive Doom captures are in `docs/images/`.
Worker-only canonical `openat2` lookup optimization passes the five-pair
stat/read probe budget, but full workloads still fail, chiefly spawn median
1.56× baseline and several p95 tails on the preceding ABI 11 guest.
Final host five pairs complete in `review-qemu-five-2`: all 10 workload/fsck
and 10 probe executions pass, with immutable hashes unchanged. Probe median/p95
ratios are stat 0.980/1.006 and read 0.929/0.863, with structural budgets met.
Full workload budgets fail: spawn median/p95 1.728/1.104, cold-open median
1.112, random-read median 1.127, random-write throughput 0.867, cold sequential
read p95 1.225, cold-stat p95 1.130 and warm-stat median/p95 1.112/1.119.
Physical five pairs complete in `review-de25-five-1`: all 14 workloads run
successfully on both releases; all held-miss floors pass. Its strict comparison
also ends BUDGET FAIL and the runner restores `194b9202`. Task 13 requires
recording that failure and stopping tuning; performance acceptance remains open.
The stopped preceding ABI 11 runtime
disk is archived with verified checksum
`5ceb30b614eb5643b2f2d61a94d343e9270f971fe3a817ef6917ddf4db9414b1`
in `review-deploy-1`; HostFS remains in place. Evidence:
`fs-phase1-performance-20261011/{worker-path-1,worker-path-five-1}`
on Beast. No performance budget is waived.

## Final five-pair performance qualification

Each cell is current/baseline, taking the median of five interleaved runs per
side. `!` marks an exact failed limit: latency at most 1.10× and throughput
at least 0.90×. Rounded values do not decide acceptance. `—` means the
workload has no byte-throughput metric. No workload or threshold was removed.

| Workload | QEMU median / p95 / throughput | DE25 median / p95 / throughput |
|---|---|---|
| append | 0.925 / 0.868 / 1.054 | 1.434! / 2.487! / 0.736! |
| durable-save | 1.006 / 1.042 / 0.983 | 1.093 / 1.559! / 0.820! |
| held-miss | 1.055 / 0.959 / 0.970 | 1.084 / 1.296! / 0.864! |
| open-close-cold | 1.112! / 1.030 / — | 1.091 / 1.485! / — |
| open-close-warm | 0.995 / 0.857 / — | 3.512! / 2.181! / — |
| rand-read | 1.127! / 1.033 / 0.998 | 1.083 / 1.088 / 0.914 |
| rand-write | 1.052 / 1.021 / 0.867! | 1.032 / 1.735! / 0.886! |
| readdir | 0.764 / 0.724 / — | 2.592! / 2.603! / — |
| seq-read-cold | 0.990 / 1.225! / 0.923 | 1.431! / 0.769 / 1.043 |
| seq-read-warm | 1.019 / 0.959 / 1.002 | 1.055 / 1.716! / 0.892! |
| seq-write | 0.983 / 0.928 / 1.018 | 1.071 / 1.340! / 0.916 |
| spawn | 1.728! / 1.104! / — | 1.401! / 1.182! / — |
| stat-cold | 1.060 / 1.130! / — | 1.257! / 1.717! / — |
| stat-warm | 1.112! / 1.119! / — | 1.622! / 1.843! / — |

QEMU baseline is the coherent `133dc6bc` tree with its own image and ELF
catalog. Physical baseline `299a6ade` changes only Linux privilege/device
setup; its QEMU/ROM/immutable guest match the original `1d7137c1` baseline.
Current guest is release `194b9202`, Filesystem ABI 12/Kit 9/STOR 36, with
the worker-only resolver optimization. QEMU's ten workload/fsck and ten
probe executions all pass; immutable artifact hashes remain unchanged.
Stat/read probes pass both latency and structural limits: stat median/p95
0.980/1.006, read 0.929/0.863, cross-AS deltas +0.002/−1.058, HostFS CPU
0 ns/op and client syscalls unchanged at 3.01/op.

Physical held-miss throughput ranges 20.064–33.413 MB/s at baseline and
20.335–26.876 MB/s current, all above the 20 MB/s floor. Current sampled
peak total QEMU RSS is 603,136 KiB, including guest RAM. A separate pressure
run holds 1,000 files and 200 directories: one descriptor/object, 3.20 and
11.52 KiB RSS/object, with fd counts returning to 32 after each close set.
The isolated provider's 30.08 KiB/DIR result remains the conservative sizing
authority; configured 2,048 files × 8 KiB plus 1,024 DIRs × 40 KiB is 56 MiB,
below the 64 MiB provider budget. These resource checks do not waive latency.

Evidence root on Beast:
`/mnt/Documents/astra68/builds/fs-phase1-performance-20261011/`.
`review-qemu-five-2` and `review-de25-five-1` retain every raw run, comparator
result and runner exit status. Both finish exit 1 because budgets fail.
Earlier missing-fsprobe attempt `review-qemu-five-1` is retained; no QEMU ran
before that preparation failure. No further guest hot path was changed.

Earlier physical spawn profiles use the preceding ABI 11 source and are
diagnostic only: whole command instructions grow 79,089,286→102,788,170;
largest increments are storage copying and cache work. They do not establish
an isolated spawn cycle budget or explain the newer board path regressions.
Follow-up must measure those exact board paths before proposing changes.

## Recovered checkpoint

Tasks 1–4 and the scoped direct lane were complete. Claude stopped during
Task 5's second review fix. HostFS is the control plane: grants are ports,
each grant owns a rooted provider session and a kernel scope, and client
channels carry data directly to QEMU. Keep that path: the rejected relay
cost about 2.6 times as much per operation.

The pending K1 fix prevents an invalid rights mask from publishing a
rightless detached handle and panicking the release kernel. The shared
transfer validator now requires a right besides TRANSFER to survive import
filtering, for masked and unmasked sends. PORT_CALL replies roll back on
refusal. Tests cover disjoint and TRANSFER-only masks, invalid second
dispositions and addresses, reply retry, and preservation of sender handles.

The continuation caught one additional case in the pending fix: an import
filter could leave only TRANSFER, which its old `imported != 0` check
accepted. The added regression failed before the check was tightened.

HostFS reaps orphan grants before pumping a request and checks whether
reaping changed the selected wait-set slot. The new booted
`raw-orphans-death` case covers a dead deriver and a grandchild grant.
The lane gate subtracts measured background traffic from 500 stats and
requires at least 400 attributable scoped commands.

## Verification

On Beast, the focused handle/process tests, complete kernel and userspace
suites, NDK contract check and NDK documentation check passed. The rebuilt
ROM and QEMU passed all 30 t9 integration gates plus the desktop, provider
index and event checks. FSSEM passed all 11 cases, with 500 attributable
scoped commands for 500 stats; the provider image passed `e2fsck`. The
filesystem stress gate passed 1,600 operations, and the privileged mount
helper passed its 20 checks with no leaked mount or directory.

The initial verification wrapper incorrectly expected 31 `EXIT=0` markers;
that extra marker belonged to an older wrapper, not a gate. The corrected
checker verified all 30 against the completed log, then ran the userspace
and NDK checks. No integration rerun was needed. Source hashes, artifact
hashes, the working diff and logs are retained under
`/mnt/Documents/astra68/builds/fs-phase1-task5-resume-20261010/`.

Fresh stat/read performance gates **did not pass**. Both runs used five
interleaved direct/lane pairs and the existing 10% latency and structural
budgets. No product code or threshold changed between them. Ratios below
are lane/direct; each side is the median across its five runs.

| Operations per run | Stat median / p95 | Read median / p95 | Failing budget |
|---:|---:|---:|---|
| 5,000 | 1.021 / 1.072 | 1.114 / 1.062 | Read median; read switches +1.138/op |
| 40,000 | 1.043 / 1.227 | 1.029 / 1.029 | Stat p95 |

HostFS CPU remained zero ns/op and syscall counts were unchanged in both.
The longer run's switch deltas passed (+0.009/stat, +0.010/read). The first
run's direct read switch counts ranged 0.052–1.636/op and the lane's
0.013–1.993/op, with operation windows of 146–220 ms. The longer run was
a diagnostic for that scheduling variation, not a replacement for the
failed run. Both failures remain evidence; neither establishes passing
performance. See `probe-comparison.txt` and
`probe-40000/probe-comparison.txt`, plus their JSON and raw logs.

Claude's last ten-pair direct/lane comparison passed: stat median/p95
0.948/0.945, read 0.981/0.989, HostFS zero ns/op, and unchanged syscall
counts. His single full-workload comparison flagged sequential-write p95
at 1.13 times the Task 3b baseline. That result is retained in
`fs-phase1-task5-rr-20261009/t5rr-vs-t3b.txt`; it is not a passing full
performance qualification. Task 13 requires at least five interleaved runs
per side and the physical DE25 comparison.

## Retained Task 6–7 continuation (historical)

Task 6 now has a canonical Linux descriptor walk. It preserves dots and
trailing slashes until lookup, follows at most 40 links across providers,
and returns the actual unresolved absolute Astra continuation. Original-byte
escape accounting survives nested links. Directory ancestry checks reject a
base moved outside its grant before lookup, truncation, or enumeration; the
final open uses `openat2` beneath the held parent without symlink following.
Mutation operations resolve the final entry, preserve source entries on
refusal, and protect the process's read-only virtual root. The shared Kit
walker keeps the first union entry's ownership of a dangling symlink.

Storage protocol 31 (minimum 31), host protocol `0x0001000f`, and Filesystem
Kit 8.0 were the uncommitted contracts at that checkpoint. LOOKUP carries a remaining
link budget. The reply embeds one 72-byte native metadata record beside its
continuation; inline capacity 208 carries the full 191-byte native path.
Native stat/lstat/stat-at/open-file stat and POSIX retain provider inode,
device, birth generation when known, allocation, full link count, and times.
Validity bits distinguish unknown metadata from real zeros. RAMFS, ext4,
PROC, METRICS and EVENTS explicitly publish their known mode and link count;
ext4 also marks ownership. The existing compact directory wire format does
not carry extended metadata. Every in-tree consumer now requires ABI 8.

The VFS and resolver host suites and their ASan/UBSan checks pass on Beast.
RAMFS tests distinguish real mode zero and an unlinked held node from unknown;
the ext4 suite tests mode zero, then completes its image/e2fsck checks. EVENTS,
METRICS and Supervisor host tests pass with the shared metadata publisher.
Generation conversion tests cover the largest representable nanosecond value,
overflow, and negative birth time. The target linker refused the original
variable 64-bit division; constant quotient/remainder bounds preserve the
contract without adding a library dependency.

Task 6 failed checkpoints are retained under `fs-phase1-task6-20261010/`:

- Attempt 1: terminal `mkdir /home/../stuff` exposed virtual-root creation;
  the shared destination resolver now returns READ_ONLY.
- Attempt 2: terminal `ls /home/..` exposed the root lacking a backend file
  token; POSIX opendir now uses the existing native virtual-root enumeration.
  The SDL timer also failed at 108 ms. Three later runs with the proper
  `testtimer.img` fixture passed; the failure remains recorded.
- Attempt 3: a post-open `/proc` ancestry check rejected a valid 4,095-byte
  raw path. Final opens now verify their held parent and compare the opened
  inode with that parent’s no-follow entry before truncation. The raw provider
  gate passed this limit and moved-out-base refusal in attempt 4.
- Attempt 4: the timer runner used `base.img`, which never launches testtimer.
  That harness failure is preserved; attempt 5 uses the correct fixture.
- Attempt 5: the root cases pass. `rm /commands/doesnotexist` then exposed
  unlink using create-style missing-leaf resolution. A focused regression
  failed before the fix. Creation callers now request LOOKUP_MISSING;
  unlink and rename sources require an existing entry. No terminal expectation
  was relaxed. The prior probe found the access-denied text, but its expected
  exit 1 was wrong: the native rm command returns the actual Astra status.

The earlier Task 6 full runs selected only the old 11 FSSEM cases, because
the harness list had not been extended. It now selects all 18, including the
five ordered-path cases, alias identity and exact-limit continuation. It
refreshes current libraries in the scratch image before installing fssem.
All 18 cases pass in the final Task 7 checkpoint below.

Task 7 verification is under `fs-phase1-task7-20261010/`. Attempts 1 and 2
record the target DIR-size assertion and the missing division helper.
The directory’s existing allocation now owns DIR plus its native state and
two-entry batch; the libc DIR layout stays intact. Attempt 3 passed the
complete userspace host suite, VFS/resolver sanitizers and target build, and
failed two of the 18 guest cases: unlink of `/work/..` bypassed native
namespace routing, and rmdir rejected a valid trailing slash. The other 16
passed, including identity and the full-length continuation.

POSIX path removal now passes the whole path through the native resolver.
Path and directory-relative unlink share a kind requirement: zero flags for
a non-directory, `ASTRA_VFS_UNLINK_DIRECTORY` for an empty directory. Every
backend checks that requirement during removal; the Kit 8.0 signature and
all consumers change together. Host VFS tests and sanitizers pass. The ext4
test exposed an obsolete ANY-removal expectation for `/`: file removal now
correctly reports IS_DIR, while directory removal still reports BUSY. Both
refusals are tested. Native-provider metadata publication and these removal
changes are included in the final target checkpoint below.

Attempt 4 passed all 18 guest cases, the 500-stat lane check and provider
e2fsck. Its wrapper was stopped before full integration when review found the
raw provider still auto-selected file or directory with zero unlink flags.
That fallback and the superseded FILE flag are removed; raw requests now use
the same zero/file and DIRECTORY/empty-directory contract. The new raw
regression failed on the previous QEMU binary (`unlink-raw-red.log`). The
wrapper's signal exit printed EXIT=0 despite SSH exit 255; it is not a passing
integration result. Its terminal child subsequently passed all 111 commands
and exited; the canonical process check then found no remaining job before
source sync. The replacement wrapper records signal exits
explicitly and tests raw HostFS before preparing the guest image.

Attempt 5 is the retained final correctness checkpoint. The VFS and resolver
normal and ASan/UBSan suites, complete userspace host suite and target build
pass. Raw HostFS passes the new typed-removal regression. All 18 FSSEM cases,
the 500-stat lane check, provider e2fsck, and all 111 focused terminal commands
pass. The full t9 run completed its build, image, suite and final markers:
30 gates passed and one failed, the parallel SDL timer at iteration 630
(`Delay 1ms = 167 ms`). Desktop apps and lifecycle, both menu paths, remote
desktop, provider index, events, 1,600-operation filesystem stress and the
20-case privileged helper gate pass. No helper mount or directory leaked.

The failed timer log is retained as `attempt-5/timer-parallel-failure.log`.
Three isolated runs of the same binary and fixture, with unchanged thresholds,
passed: timer periods 1.043187, 1.047888 and 1.043405 ms, callbacks 99/299/64,
and one-second delays of 1,000 ms in each run. These establish isolated timer
behavior; they do not waive the failed parallel gate or establish its cause.
NDK headers/documentation and bundle provider coverage also pass.

The follow-up verified all 85 archived source hashes before and after its
checks. `attempt-5/artifacts.sha256` records the ROM, kernel, fssem,
filesystem.library.8 and QEMU hashes; each was checked again after completion.
The remote process check found no remaining verification or QEMU process.
Documentation updates after this tested snapshot are recorded separately.

The pending Task 5 correctness fix is tested. User direction on 2026-10-10:
finish correctness before performance qualification. Keep all measured failures
for the later warm path and Task 13. Functional passes do not qualify Phase 1
for release. No performance optimization, commit, push, merge, or DE25
publication has been made in this continuation.

Later correctness obligations remain: shared open descriptions and fork
adoption (Task 8), retained cwd (Task 9), provider loss and recovery of the eight per-channel scope slots
(Task 10), removal of the shared 64-entry kernel scope partition and
per-holder admission (Task 11), and bootstrap availability (Task 12).
Warm-command optimization (Task 12b) and final performance qualification
(Task 13) follow those correctness tasks. HostFS's grant table grows; do not restore the deleted grant
ceiling. Runtime 1.15 belongs to send dispositions, so Task 11 takes 1.16.

When integrating boot splash, retain both syscall features and ABI
`0x00010043`. Run the full merge verification. This continuation has not
published or changed the DE25 runtime.


## Task 8 continuation

Current branch contracts are storage protocol 32/minimum 32, host transport
0x00010011/metrics 4, Filesystem Kit 9.0 (Filesystem.kit 6.0.0) and libc 4.0
(Posix.kit 2.0.0). Replies are 368 bytes with separate position, token and
open-flags fields; inline payload remains 208 and metadata 72 bytes.
The provider owns each shared description's Linux fd/DIR, position, cursor
and mutable APPEND/NONBLOCK flags. Positioned I/O preserves the position and
positioned writes ignore append. Sequential calls make one provider transaction
and return short transfers rather than allowing hidden interleaving.

Random one-use receipts retain a pending description reference and can only
be adopted through the same grant. Withdrawal uses the original owner and
survives closing its file. Owner death discards unclaimed references; adopted
owners stay alive. Fork shares files before clone and waits for adoption before
returning; adoption errors kill/reap the child and withdraw remaining receipts.
Directory streams rebind their borrowed client/file after adoption. Exec state
version 2 carries child wait handles and resource accounting as well as files.

A forced-moving allocator regression exposed dangling borrowed VFS clients:
growing the old client array moved records referenced by open files. The
pointer table now grows while separately allocated client records stay put.
The old code failed the identity assertion, and the fixed host suite passes.
Native eager-loader code stays within its existing closed low-level subset.
Lua/NTP targets now depend on their dependency library files, so an ABI symbol
version change relinks them instead of retaining obsolete libc references.

Evidence: `fs-phase1-task8-20261010/` on Beast. The raw shared-description gate
fails against Task 7's QEMU and passes against the new provider. `native-4`
passes VFS/POSIX host suites, the full target build and raw provider tests,
including wrong-session adoption, replay, positioned I/O, shared flags,
original-handle close, owner death and final fd release. Guest shared-description,
exec-inherit and append-race checks are running. Review also found the high-level
open validator omitted NONBLOCK; it now uses the shared mask and has a new
regression. `native-6` passes host suites, sanitizers, the target build, raw
provider checks and NDK syntax checks; `native-7` also passes documentation.
The native-7 guest run passes 18/21 and fails all three new fork cases with
ENOSYS: child lazy reconnect erased the negotiated description feature before
ADOPT. Rebind now retains that contract after verifying the inherited scope.

Review found a second shared-state defect: directory batches replayed a
caller-local cookie, and the generic entry loop could consume an entry that
did not fit before releasing its lock. READDIR now has one packed batch wire
format and capability bit 14; the provider owns the entire locked transaction
and rolls back unreturned entries. Native reads use CURSOR_CURRENT and rewind
uses SEEK. Alternating owners and small/partial-buffer rollback have raw and
host regressions. `native-11` passes host suites, sanitizers, documentation and
the full target build; its raw run found an old single-name assertion in the
test harness. The corrected harness and `native-12` pass raw checks, the full
target build, NDK syntax checks, all 21 guest semantic cases, the lane gate
(506 commands for 500 stats, three idle) and provider fsck. Source tarball,
source hashes, selected QEMU path and six artifact hashes are retained there.
No full Task 8 integration claim yet.

## Task 9 continuation, 2026-10-11

Provider `fs-phase1-task9-20261011/provider-3` passes the full raw gate,
including PATH rename/removal/confinement and existing shared descriptions.
Task 8's executable rejects PATH (UNSUPPORTED), proving the regression starts
red. Host version 0x00010012, metrics 5, PATH 24 and FS_PATHS bit 15 are now
qualified at the provider layer.

The native CWD implementation is qualified: STOR 33/minimum 33, DIRECTORY_PATH
42, Filesystem Kit 10.0/filesystem.library.10/Filesystem.kit 7.0.0; libc 4.0.
Process VFS owns the retained directory and its mutex. Native facades and
loader source reads resolve relative names through it, and the existing
namespace walker handles provider escapes. POSIX has no CWD string. Fork
uses the existing shared-description receipts; native exec state 8 owns CWD,
while personality file exec state 3 carries only POSIX bookkeeping.

Native-4 passed the build and raw gate but timed out in the shell. Target
inspection found the CWD mutex at 0x4101a: MC68040 C aligns uint32_t to two
bytes, while the native mutex requires four. An explicit _Alignas(4) fixes it;
native-6 records 0x4101c and checks alignment automatically. Native-5 was
stopped after retaining its host/sanitizer results and alignment evidence.

Native-6 passes VFS/POSIX hosts and sanitizers, resolver hosts and sanitizers,
the complete MC68040 build, raw HostFS, NDK headers/coverage and HTML/PDF.
Its first guest invocation named a nonexistent image fixture; semantics-2.log
records the corrected invocation against Task 8 native-4's base image.
All 22 FSSEM cases pass, including CWD rename/replacement, relative native
operations, fork, relative exec, parent traversal, removal and buffer limits.
The lane gate reports 509 commands for 500 stats, three idle; provider fsck
passes. The guest runner exited zero, and source archive/hashes, QEMU path,
mutex address and six artifact hashes are retained in
`fs-phase1-task9-20261011/native-6` on Beast. The archived source predates
local Task 10 work. Tasks 10–12 and broad integration remain;
performance qualification stays deferred.

## Task 10 continuation, 2026-10-11

Recovery is staged at STOR 34/minimum 34 and Host 0x00010013, keeping
Filesystem Kit ABI 10.0 and libc ABI 4.0. The current Task 10 plan describes
the final implementation; superseded pseudocode was removed. Resumed HELLO
retains the caller/grant control session and reply lane, replacing only the
scope. HostFS verifies recorded root identity before recovery. Lost handles
remain owner-bound and support one-use SHARE/ADOPT inheritance without path
reopening. Supervisor uses delivered publication names for managed-holder
restart and stable client cells for publication registry growth/deletion.

Evidence is `/mnt/Documents/astra68/builds/fs-phase1-task10-20261011/` on Beast.
Native-2 passed host/sanitizer/build and earlier raw tests but the new raw
fixture created mode 000, correctly refusing its second open. Native-3 uses
0644 and passes all host/sanitizer/build/raw gates, including twelve provider
resets and disconnected child adoption, NDK checks and HTML/PDF, all 22 FSSEM
cases and lane (509 commands for 500 stats, three idle). The provider-reset
probe recovered fresh opens and retained old lost identities, then fork
failed ENOSYS: an unused parent client still carried a revoked scope, whose
query was misreported as UNSUPPORTED during child rebinding.

Native-4 passes the complete loss probe, including fork, with proper LOST
mapping and child refresh. All 22 cases and lane pass. Its existing shell's
next fork fails because SHARE skipped scope refresh; the probe had already
refreshed through fstat. The new host guard regression fails in
share-first-red.log and the full VFS suite passes in share-first-green-2.log
after fixing the common guard. Area exchanges retain their outer data guard
to avoid nested refresh. Native-5 passes hosts/sanitizers/full target build,
raw gate, NDK check and HTML/PDF, all 22 cases/lane and provider reset. Its
HostFS-death completion marker times out; no cause is claimed. Native-6
passes isolated HostFS death and fsck. Native-7 passes the full sequential
22-case/lane/reset/death/managed restart/fresh Terminal/fsck gate, with exact
source archive and six artifact hashes. Task 10 correctness is qualified.
Prior failures remain retained; Tasks 11–12 and broad integration follow.
Performance acceptance remains deferred.

Task 11 kernel/VFS hosts, VFS ASan/UBSan, target build and raw admission
pass. Native-6 passes 25 guest cases, 16 lanes, owner session/file limits,
1000-file/200-DIR/16-control-session holds, full-quota reset, shell/desktop,
lane509/500 idle0, provider reset after585, HostFS death after703/managed
restart/fresh Terminal and fsck. Evidence and exact identities:
`/mnt/Documents/astra68/builds/fs-phase1-task11-20261011/`. Native-5's
`/dev/null` fixture failure is retained. Measured whole-QEMU RSS growth is
2.74 KiB/file and 17.18 KiB/DIR; native-5 gives 4.368/17.0. It includes guest
workload memory, so it does not set DE25 bounds. Counters and host FDs return
to baseline; control sessions share their grant's provider session.
Runtime1.16, syscall10043/HOST_SCOPE_LIMIT112 (bound refusal INVALID_ARGUMENT),
Host10014/config4/scope_filesbyte48 now agree. Lost IDs/receipts remain bounded,
failed adoption keeps its token, and quotas precede Linux create/truncate.
Cache cells stay stable/borrowed until quiescent teardown; facade close only
detaches. Features/activity are atomic without a target layout change. Scopes
use reusable metadata pages; DMA16 uses first-fit pages in the unchanged
window and passes the process size guard. New HostFS settings negative and
sanitizer checks pass in Task12 native-1.

Task 12 passes in `/mnt/Documents/astra68/builds/fs-phase1-task12-20261011/native-3`:
host cleanup/settings tests, sanitizers, full target/raw/NDK HTML/PDF, all
25 semantic cases and budgets/flood/lane/reset/HostFS death/fsck, exact/absent/
mismatched bootstrap, and DE25 cross-build. Compare bootstrap bytes before
manifest_parse mutates them; native-2's false mismatch remains retained.
`de25-calibration-2` passes the full raw gate and isolated AArch64 memory holds
as UID/GID65534. DIR growth30.72KiB; file RSS reused memory and is not an
allocation-size result. Counts/FDs return exactly to baseline. The raw qtest
write baseline median12.529ms includes Python/socket/doorbell overhead, not
native MC68040 or application latency. Launcher bounds are staged at
2048 descriptions/objects and1024 DIRs (8KiB +40KiB extra/DIR =56MiB), with
QEMU clamping to RLIMIT_NOFILE minus256. Native-3 and calibration finished
exit0; no owned QEMU/build jobs remain. Broad t9 integration remains pending.
New 26th FSSEM case `file-size-limit` fails in `append-before` (child status256),
without identifying which operation failed. Native-12 later identifies signal
handler installation before any write. The code audit separately finds finite
RLIMIT_FSIZE uses cached current position instead of atomic append EOF,
and zero writes fail at limit. Correct this in the canonical native write
contract, not a POSIX fallback. RAM backend also used OPEN_APPEND instead of
WRITE_APPEND; its new host regression starts red and the one-line fix is staged.
Ext4 must honor positioned writes on an append-open handle and atomic per-write
append. Performance12b/13 stays deferred.
Read-only DE25 inspection finds production release1d7137c1 running QEMU as
root. Preserve that unrelated live release during isolated probes; new
release acceptance requires an unprivileged launcher and complete provider
permissions. No commit/push/deploy made.

### Atomic write bounds and fork correctness, 2026-10-11

Current staged contracts: Filesystem ABI 11.0/Filesystem.kit 8.0.0,
STOR 35/minimum 35 (request 248, two-path 440, reply 368/payload 208),
Host 0x00010015/configuration 4. Every write takes exclusive `max_end`,
UINT64_MAX unbounded. Inline, bulk and direct transports carry it to the
provider. Existing namespace/mount/filesystem locks serialize bound and write;
independent append descriptions cannot race past a finite limit. POSIX passes
RLIMIT_FSIZE and translates no-byte refusal to EFBIG/SIGXFSZ. Zero writes do
not inflate size or shift position. Positioned writes ignore APPEND.
All in-tree callers/manifests/exports/fixtures moved to ABI 11; no old API path.
lwext4 patch 0024 shares one write implementation for its upstream and native
bounded/positioned entries. Native-16 passes kernel/VFS/POSIX/ext4/settings/
Supervisor hosts and sanitizers, full MC68040/QEMU build, raw provider, NDK
contracts and documentation, all 26 FSSEM cases and budgets/flood/lane/reset/
HostFS-death/managed restart/fsck. Bootstrap match/absence/mismatch and the DE25
cross-build pass. Native-16 finishes exit zero with all 164 source hashes
unchanged. DE25 provider qualification passes in `de25-calibration-3`; full
t9 integration passes in `integration-1`. Exact source
archive and logs are under `fs-phase1-task12-20261011/native-16` on Beast.

Native-11 failed the combined limit test and then quota1023 because the parent
test descriptor was left open. Native-12 closes it before the assertion, passes
quota1024 and identifies stage3: SIGXFSZ handler installation failed. Kernel
signal configuration directly probed a fork COW stack as read-only; it now uses
the existing writable-user-buffer preparation. The red kernel regression is
`signal-cow-red.log`. Native-13's cleanup used invalid signal zero; after the
fixture uses SIGKILL, native-14 exposes suspended READY retirement corruption.
The shared retirement branch now skips queue removal for suspended threads.
Native-15 reaches reaping and fails: a parent COW write leaves the child's
original page owned by the parent. `signal-debug.log` records forced release
and reaper corruption. COW replacement now reuses `cow_rehome_before_unmap`.
Tests cover both writers, data survival after parent release and exact memory
recovery. Complete kernel and guest gates pass in native-16; all failures remain
retained. POSIX write/pwrite/ftruncate use existing raise(SIGXFSZ), avoiding
recursive bounded PROC writes through kill(). Guest checks cover tiny-limit
PROC refusal without priority mutation and refused ftruncate without size change.

Production launcher defaults: 2048 objects/descriptions, 1024 DIRs (56 MiB
conservative budget); QEMU reserves 256 host FDs even with explicit settings.
Launcher regression gate passes after fixing process_alive's bare return in a
Bash EXIT trap, which inherited status 143 and skipped killing live helpers.

The current AArch64 QEMU passes bounded concurrent writes, confinement,
quotas, twelve resets and bootstrap handover as UID/GID 65534. Calibration-3
holds 1000 files and 200 DIRs and returns all FD/description/DIR counters to
baseline. DIR RSS growth is 30.08 KiB each; reused file memory does not measure
allocation size. The raw qtest write diagnostic includes Python/socket/doorbell
overhead and is not native performance qualification. The live production
release was not changed.

Integration-1 also passes the formerly failing parallel SDL timer with the
unchanged thresholds: 1.051856 ms period, callbacks 99/299/64, one-second delay
1000 ms. Task 7's earlier 167 ms failure remains retained; no causal claim is
made for the later pass. Doom passes 30 seconds of gameplay, keyboard input,
locked/hidden pointer, audio and host MIDI, and clean ENDOOM quit without faults.
This is QEMU evidence, not new physical gameplay or display qualification.

Public visuals remain scheduled after stability: use the existing 1920x1080
DE25 splash; replace the ten SDL bundle icons currently sourced from upstream
`test/icon.bmp` through the existing PNG/.aicon pipeline, retaining the demo
resources; capture the real desktop and Doom through Beast. Do not use old
captures or generated mockups as current runtime evidence.
No production release changed. Performance 12b/13 and public image/icon refresh
remain deferred until correctness is stable.
