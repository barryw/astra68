# General file cache and file-backed mappings

Status: design contract; VFS description retention, metadata, VM page
publication, canonical HostFS reads/writes and bounded writeback pass host and
DE25 gates. Checkpoint82 is installed in the selected development shadow;
[Current State](CURRENT_STATE.md) records its exact identity and evidence.
Logical timestamp persistence and saved-error precedence pass on the
qualified ext4 path. Native owned/external EOF integration passes checkpoint72.
Native/POSIX shared writable mappings and range sync pass host/DE25 gates.
Exact mapped-user attribution and shared shutdown/cancellation/recovery UI now
pass host, native and DE25 gates. Physical USB reattachment also passes. The
current optical-drive/disc regression remains open; broad performance
qualification and network mounts remain outside this correctness task.

Explicit-unmap error delivery79 is DE25 qualified. Native exact
and range unmap reuse retained pager ASYNC checks before removing shared views.
A hard backing error leaves the requested range intact; no second cache,
error ledger, worker or POSIX mapping inventory is introduced. The canonical
area mutations advance a nonwrapping address-space sequence. A blocked check
restarts validation if views change, under its original deadline, before the
existing unmap transaction commits. Provider-owned/private/anonymous views and
holes avoid provider calls. Dirty generations retain pending backing work;
unmap is not a durability barrier. Process-death cleanup remains nonblocking.

Haiku `src/system/kernel/vm/vm.cpp:5971-6001` holds the address-space write lock
through unmap; `:6228-6318` retains backing while releasing that lock for I/O.
Astra adapts these patterns by revalidating its existing view records after
bounded asynchronous checks. Host tests cover exact/range errors, holes,
second-view failure without partial removal, timeout, abandoned replies,
provider self-call avoidance and a concurrent client mapping. Native/POSIX
integration injects errors, preserves mapped bytes and dirty state, then retries
successfully. Sanitizers, 35 MC68040 markers, NDK consumers and all 45 native
cases pass. DE25 repeats the new native/POSIX error-retention probe and existing
storage/mapping checks, then shuts down 88 at 530/600 seconds. All four gate
components succeed. Archive
`d18b166c83f961aec4449d2d1e4ab01b6a25acbc8ead4ac1954f8ce04f85edef`
verifies 577 hashes, exact manifest, paired settings/key, unchanged boot/selector
and read-only fsck before cleanup. Fresh selected stage 8, three active services
with zero restarts, only loop0 and 8.6 GiB free pass. Failed setup/control evidence
is retained; the corrected probe respects the noncloneable receive endpoint and
the tightened old-kernel control fails for the intended unmap error. Candidate
identity and evidence paths are in CURRENT_STATE.md; integrated deployment and
the fixed remaining filesystem list stay open.

Mapped-file EOF/fault termination 78 is DE25 qualified. Empty backing and
initially beyond-EOF reservations are admitted; actual access follows the
existing authorized native view and backing length. The side-effect-free
reserved-mapping query supplies the native backing-fault verdict through the
common fault-report/retirement path. Denied writes and unmapped holes retain
the ordinary verdict. POSIX death waits translate these to SIGBUS/SIGSEGV;
this change adds no in-process hardware-exception handler or kernel POSIX
policy. Haiku `vm/vm.cpp:2248-2419` supplies ownership/access patterns; its
shared-EOF admission restriction is deliberately adapted here.

Host regressions reproduce the former rejection and incorrect fault status.
Kernel/VFS/POSIX host, ASan/UBSan, 34-marker MC68040 and NDK gates pass. The
classification query measures 32/100000 cycles; existing budgets still pass.
All 44 native cases, pressure/publication repetitions, provider/HostFS recovery,
exact backing bytes, shutdown 88 and fsck pass. Tests cover empty/past-EOF
views, cross-owner growth, shrink revocation of private copies, shared/file
zeroes, private isolation, close/unlink and precise fault termination.

Retained test corrections preserve COW edits across growth and use a compact
result marker that fits the documented rendered-row echo contract. Raw trace
and Haiku VMCache::Resize establish those expectations; no product workaround
is introduced. DE25 repeats the new probe and existing storage/mapping checks,
then shuts down 88 at 510/600 seconds. Archive
`64a4b0997c540544828aa3ec4741cdf1dfde59e8869c88aa87af32b22560d15e`
verifies all 577 hashes, exact manifest, paired settings/key, unchanged boot and
selector, and read-only fsck before disposal. Fresh selected stage 8, three
active services with zero restarts, only loop0 and 8.6 GiB free pass. The
selected release and user data are unchanged. Identities and remaining gates
are recorded in CURRENT_STATE.md; evidence is
`/home/barry/astra-evidence/cache-mapped-eof-20261115/`.

Native range sync77 is DE25 qualified. Haiku
`src/system/kernel/vm/vm.cpp:6228-6318` resolves native views, retains backing
and releases the address-space lock before sync. Astra uses existing view
records and area child references across the normal bounded pager call.
Additive syscall130 and the Runtime export work without file descriptors or
temporary area handles. Private/anonymous views skip backing I/O; holes fail.
Typed40-byte SYNC/SYNC_REPLY operations leave PAGE fields and its residency
completion unchanged. Success advances across views under one30-second
deadline; failures preserve exact backing status and dirty state.

HostFS transfers the existing reply capability through a private control port.
Only its control worker invokes retained-file sync, native node I/O and the
canonical writer. Page fills remain available; no second cache, writeback
worker, POSIX mapping ledger or pending-message inventory is added. Full queues
return BUSY for bounded native retry. Full sync may flush the existing larger
file-cache extent, matching ordinary file sync. ASYNC refreshes retained owner
metadata, surfaces saved hard errors and acknowledges existing dirty-generation
work without waiting for backing writes. That work owns pending data after
the final unmap. Shutdown drains sync requests before cache retirement; error
outputs do not race the page-fill thread's status.

POSIX shared writable mmap/msync adapt the native owner. Synchronous mode waits
for backing completion; asynchronous mode waits for admission. MS_INVALIDATE
uses native owner/alias coherence; private copies never write shared backing.
Permission, alignment, flag, hole and backing errors retain their proper errno.
Explicit-unmap saved-writeback-error delivery is now qualified by checkpoint79
above; retaining dirty backing alone is not treated as error delivery.
Initial empty/beyond-EOF mappings and backing-fault termination are now
covered by checkpoint 78 above. Fixed placement and no-access/execute mappings
remain explicitly unsupported; no claim of complete POSIX mmap is made.

Reference-capacity tests reproduce the existing16-bit child count reporting
CORRUPT at its legitimate maximum. Canonical retain now returns NO_SLOT with
unchanged references. Ring admission rolls back its unpublished slot and
accounts failure; display, prefetch and sync preserve resource exhaustion.
Haiku `headers/private/kernel/vm/VMCache.h:311-344` and
`headers/os/support/Referenceable.h` supply the owner-reference pattern; Astra
keeps its compact counter and explicit capacity status. Area/ring tests fail
before correction; kernel host and area/ring/process ASan/UBSan pass afterward.

All33 K1 markers, normal ROM and NDK dist-check pass. View resolution83/100000,
request1598/50000, reply2786/75000 and handoff86/25000 cycles stay within declared
budgets. Seven userspace products remain unchanged after NDK qualification.
Full native43-case integration, pressure/publication repetitions, external EOF,
both recoveries, independent backing bytes, shutdown88 and fsck pass. DE25
repeats shared native/POSIX checks around pressure and passes the common cache,
private/anonymous/partial, EOF and shutdown gates at464/600 seconds.

The first DE25 trial stops on a truncated writeback marker despite successful
command status; its kernel is idle, not panicked. A regression reproduces the
shared observer consuming an unfinished continuation. The reader now commits
only complete lines and rereads pending prefixes. The same binaries and image
pass with that correction. Both failed and successful evidence are archived
and verified before disposal. Final577 member hashes, exact release manifest,
paired settings/key, unchanged boot/selector and read-only fsck pass. Board
cleanup restores fresh selected stage8, three active services with zero
restarts, only loop0 and8.6GiB free. User data and selected release are unchanged.
Exact identities and remaining gates are recorded in `CURRENT_STATE.md`;
evidence is `/home/barry/astra-evidence/cache-range-sync-20261115/`.

Native shared-write authority76 prepares the existing retained cache writer
before HostFS hands out WRITE on a read/write file's canonical area. A
read-only description cannot acquire WRITE through another writer's cache.
Failed preparation returns no area. A private cache-preparation flag records
that external stores are possible until the cache retires; it survives backing
revision refresh and does not count mappings or replace VM ownership.
For that policy, backing writes supply modification timestamps instead of
reapplying an older timestamp from ordinary cached I/O. Ordinary-only caches
retain their existing exact timestamp persistence. Haiku
`kernel/vm/vm.cpp:2248-2334` ties shared write permission to a read/write
description and retains the vnode cache; `:6228-6318` resolves sync through the
current address space and native cache. Host regressions fail the old rights
and timestamp behavior. VFS/HostFS host/sanitizer/target and normal-ROM gates
and NDK headers/exports/C/C++/HTML/PDF gates pass. Native/DE25 gates pass below.
POSIX shared writable mappings
remain guarded until the native address-range sync contract is complete.
The first native run catches common reply narrowing still stripping WRITE.
The shared VFS reply path now preserves prepared WRITE only within the grant's
WRITE ceiling, with positive and read-only-grant negative transport tests.
VFS/sanitizer/target/NDK repeat passes. A later native attempt reaches WRITE
and catches the probe requesting a different-permission alias through the
idempotent map API. It now uses the existing explicit-address API for that
independent view; no additional OS mechanism or policy changes.
Final native76d passes all 42 cases, pressure/publication repetitions, external
EOF, both recoveries, exact backing-byte checks across closed descriptions,
fork, growth and 128 MiB windows, shutdown 88 and fsck. All seven products
match the immutable snapshot. An initial stage fails on generated fixture
ownership before a trial; ownership is corrected without changing modes or
release content. Later fixtures are created with caller ownership.
DE25's shared-write/backing check passes, then external EOF fails because its
host mutator's single30-second deadline expires before the second marker at
37 seconds. The file remains4105 bytes: truncate-to-one never ran. The
corrected test uses a deadline per stage and explicit host completion before
starting EOF-fault/growth checks; all functional assertions are retained.
Diagnostics and the failed image are preserved; no panic occurs. That failed
attempt supplies neither hardware acceptance nor evidence of an OS EOF defect.
Failed-trial archive verifies61 hashes, manifest/key/boot/selector and read-only
fsck status0 before board cleanup. Corrected probe waits for host completion
without opening the target, retains four-second fault/growth checks, and uses
per-stage host deadlines. The command bound is120 seconds. A proposed900
overall guard is rejected before boot by the helper's600 limit; waiting
observers are stopped, logs preserved and the supported600 guard restored.
The already staged image is reused. Kernel budgets do not change. Only the
test executable needs rebuilding.
Final DE25 preflight takes36.174s and stage8 arrives at72.3s, QEMU UID/GID999.
Both shared-map passes verify native authority, closed-description/handle
lifetime, fork/coherent I/O, EOF/tail zeros and128MiB owner windows against
actual host backing bytes. External EOF, writeback/timestamps/prefix, eight
pressure/session cases, private/anonymous/partial mappings, publication/root
checks and shutdown88 pass at436/600 seconds; all four components return zero.
READ1543326/4000000 and WRITEBACK8555241/12500000 cycle budgets pass, with no
speed claim. Final release
`ac8eedc3bc3781f32c31370465966db1223cb6303d51546ac2392ca62f2d456c`
matches all seven current products. Archive
`66bdd3207b79826aaadb1812613bdd72b5601daefb86dc28a9a49c624dacbc0f`
verifies573 hashes, exact manifest, paired settings/key, unchanged boot/selector
and read-only fsck. Selected release/user data remain unchanged.
Disposable board state is removed only after archive verification. Fresh
selected stage8, three active services with zero restarts, only selected loop0
and8.6GiB free pass; no Astra build/test process remains on Beast. Native
address-range sync and POSIX shared writable mappings/msync remain open.

Native range unmap75 trims or splits existing page-aligned view records.
Split metadata is reserved before mutation, so allocation refusal changes no
view. Full shared-table spans keep their common translation. A boundary cut
detaches only the caller's span; surviving fragments fault existing shared-frame
aliases into process-owned leaf tables. Reclaim, snapshot protection and EOF
withdraw those aliases through one helper. Private copies retain their existing
COW rules. Native backing/reference ownership remains authoritative; POSIX's
mapping registry, allocations and mutex are deleted. Initial padding is trimmed
so native views describe the exact POSIX page extent. Syscall129 and Runtime's
range wrapper are additive. Mapping counts widen to32 bits; native330-byte
records still use one metadata frame per eight objects.
Haiku `vm/vm.cpp:762-926`/`:1095-1130` supplies trim/split/backing retention;
`headers/private/kernel/vm/VMCache.h:242` uses a32-bit reference count.
Host/sanitizer/Runtime/POSIX and31-marker K1 pass. Range removal costs519549/
2000000 cycles; existing budgets remain unchanged. Final native all41 passes
pressure/publication repeats, both recoveries, post-recovery partial/private/
anonymous probes, shutdown88 and fsck. Old-libc control fails the same interior
cut and shuts down cleanly. Final NDK dist-check passes headers/exports,
C/C++/SDL CMake, static/shared and HTML/PDF gates. Doxygen graph concurrency
is explicitly two workers to fit the container's two CPUs and96-task limit.
NDK's libc rebuild initially makes snapshot verification refuse the tested
input75b. Refreshed input75c passes the complete native gate again, and all
seven image products match the final built files. DE25 then passes anonymous
and partial-unmap probes twice, external EOF, writeback/timestamps/prefix,
eight packed pressure/session cases, private mappings/fork/cross-owner copies,
final publication/root checks and shutdown 88 at 501 of 600 guard iterations.
All four gate components return zero. Existing READ 2223138/4000000 and
WRITEBACK 3366926/12500000 cycle budgets pass; no speed gain is claimed.
Archive
`16677665355f55851ba9dad682a358ea13f3ee1400bdf95a7f921427e390954a`
verifies 571 hashes, exact manifest, paired settings/key, unchanged boot/selector
and read-only fsck before disposable board state is removed. Fresh selected
stage 8, three active services with zero restarts, selected loop0 only and
8.6 GiB free pass. Selected release and user data remain unchanged; shared
writable mappings/msync, complete POSIX fault/EOF, mapped-user attribution,
shutdown UI and final media/release gates remain open.

Native mapping lifetime74 adds one create-policy flag to existing area
references, independent of reservation or private-copy mode. Final unmap
retires backing after handles and children close; fork and exec use their
existing VM/area teardown. POSIX anonymous private/shared maps use that policy
and the existing mapping registry, with no retained temporary handle or second
allocator. Zero fill, rounded tails, ignored descriptors, private/shared fork
behavior and retirement have native probes. Haiku
`kernel/vm/VMCache.cpp:1002-1034` and `vm/vm.cpp:1328-1362` guide retaining
backing through views. `kernel/port.cpp:850-886` guides pager-owner exit:
revoke all clients before releasing backing, cancel dead-owner dirty snapshots,
and retain only terminal metadata while handles or requests remain. Creator
exit alone never substitutes for actual pager-owner death. Live dirty owners
retain their existing pin until writeback completes.
Host, ASan/UBSan, Runtime and30-marker K1 checks pass; DE25 also qualifies74.
The area remains328 bytes. Predeclared budgets pass: mapping lifetime2351/
100000 cycles, sparse128MiB pager retirement681096/2000000. A pool validator
initially rejects a closing truncated area's former backing extent; the live
area rule is now scoped correctly. A late completion reports PEER_DEAD before
checking pager residency. The initial K1 fixture passes null required output
arguments to port-owner teardown; correcting those arguments resolves that
test failure. Final native all40, three extra pressure/session rounds,48
publication races, external EOF, provider/HostFS recovery, post-replacement
anonymous/private probes, shutdown88 and fsck pass. DE25 passes anonymous
maps twice, external EOF, writeback/timestamps/prefix, eight packed pressure/
session cases, private mappings/fork/cross-owner copies, final publication/root
checks and shutdown88 at462/600 guard iterations. All four observers return
zero. Existing READ2571215/4000000 and WRITEBACK3580049/12500000 cycle budgets
pass; no speed gain is claimed. Archive
`733280837274967b08b93dd5a7607ad9da88d1967f2120aec102b4a6755f698f`
verifies571 hashes, exact manifest, paired settings/key, unchanged boot/selector
and read-only fsck before disposable board state is removed. Fresh selected
stage8, three active services with zero restarts, selected loop0 only and8.6GiB
free pass. Remaining mappings/UI/media/release gates still block promotion.

POSIX private file maps73 pass READ/WRITE/PRIVATE permissions to the existing
native COW path for every selected source window. Writable copies require file
read access, including when the descriptor itself is read-only; source write
authority is unnecessary and is never manufactured. Write-only files remain
ineligible. On MC68040, write permission implies read permission. No additional
cache, dirty tracker or kernel mechanism is added. The old-libc RED rejects
this operation. Focused native probes cover close/fork isolation, preservation
of canonical bytes and a two-window boundary. Host/API/target checks pass.
Full native all39, three pressure/session rounds,48 publication races, both
recoveries, post-recovery private copies, shutdown88 and fsck pass in149.8s.
DE25 passes external EOF, writeback/timestamps/prefix, eight packed pressure/
session cases, native/POSIX private copies across close/fork/owner boundaries,
final publication/root checks and shutdown88 at448/600 guard iterations. All
four observers return zero. Existing cycle budgets pass; no performance gain
is claimed. Archive
`1d80448e4c769ea59f7392b11ea6e484dcc8f8717b4285e8d48e67a4f54532d6`
verifies571 hashes, manifest, paired settings/key, unchanged boot/selector and
fsck before board cleanup. Fresh selected stage8, three services with zero
restarts, only loop0 and8.6GiB free pass. Shared writes, anonymous mapping
lifetime, partial unmap and msync remain separate unfinished contracts.
Haiku `libroot/posix/sys/mman.cpp:109-181` translates flags into native VM
operations; `kernel/vm/vm.cpp:762-926`/`:1095-1130` trims or splits views while
retaining backing. Astra must use the same ownership pattern with its native
view records. Shared tables cover256KiB; area/private slots cover4MiB. Clearing
a shared table for one partial unmap would revoke other users' pages.

HostFS EOF integration72 joins the existing native window owner to backing
length. The private service-core cache metadata callback runs under node I/O
without the state lock; open/stat/retained-stat/truncate all use it. HostFS
initializes an unpublished area's length before returning it, publishes
accepted logical growth across sibling windows, and refreshes external EOF
through the same retained-stat seam. The former HostFS-only truncate wrapper
and invalidator are removed. A revision refresh checks/reclaims every matching
window before resizing any, preserving private copies when a later dirty
window refuses the external change. Existing dirty conflict/error policy and
pager epochs remain authoritative. The callback table is private static service
code, not an NDK record or shared-library export; no public ABI changes.
Haiku `cache/file_cache.cpp:1387-1401`, `vm/VMCache.cpp:1188-1228`, and
`fs/vfs.cpp:4074+`/`:6835-6870` guide EOF and native ownership. Host/sanitizer
and target gates pass. Native external Linux changes, with the guest's target
descriptor closed, prove private partial-tail zeroing, child-only full-page
EOF faults and fresh growth without target I/O. An old-provider control fails
the new owned-truncate/fork probe. Final native all39, three pressure/session
rounds,48 publication races, both recoveries, private maps, shutdown88 and fsck
pass in156.3s. DE25 passes external idle-map EOF, writeback/timestamp/prefix,
eight packed pressure/session cases, private mapping/fork/reclaim and owned
EOF, final publication/root checks and shutdown88 at441/600 guard iterations.
All four observers return zero. Fixed cycle budgets pass; writeback's two-sample
measurement is higher than baseline, so no performance gain is claimed. Archive
`0d2c0290e96b688249ecdd46eeb8a1163a42ec12c17f7399e5b1b6eafe09a3bd`
verifies571 hashes, manifest, paired settings/key, unchanged boot/selector and
fsck before disposable board state is removed. Fresh selected stage8, three
services with zero restarts, loop0 only and8.6GiB free pass. This qualifies
native EOF integration; POSIX mapping and filesystem release gates remain open.

Private area views69 reuse the native area/VM owners. ASTRA_AREA_MAP_PRIVATE
needs READ and MAP authority, including writable private views. Reads alias a
canonical page read-only; first writes reuse the ordinary process-page COW copy
path. Existing fork COW, owner rehoming and teardown handle those copies. Clean
source reclaim removes only unchanged aliases, preserving private bytes. Shared
L3 tables cannot be replaced privately, and private views cannot decommit their
source. No second byte store or dirty ledger is added. Haiku's file-map access
and vnode/cache ownership pattern supplies the reference. Host/sanitizer and
K1 gates pass, including old-kernel rejection and unchanged existing cycle
budgets. The focused native file-map/fork/source-invalidation/shutdown/fsck gate
passes. Automatic shared and private views coexist; idempotence is per mode.
The full run exposes a session/grant failure after pressure. Original client68
and original kernel68 controls also fail with current input. Channel authority70
uses the existing binding ledger to reopen drained channels when moving between
scoped data and device control, including batches; failed reopen releases DMA.
Strict mock REDs cover both transitions, batch admission and cleanup. HostFS
also schedules housekeeping from completion so missed periods cannot starve
queued control requests. Haiku's periodic timer skips missed ticks similarly.
Host/sanitizer/target checks and the first full native all39/recovery/private-map/
shutdown/fsck gate pass, but its repeat stalls. The captured service owns all255
handles and cannot import a queued volume request's two handles. Volume and
filesystem pumps now share the existing bounded receive-pressure reclaim and
retry policy; HostFS supplies its existing writeback/idle-owner reclaimer.
The kernel leaves refused messages queued. Tests cover reclaim success,
ineffective bounded retry and WOULD_BLOCK. The next full39 passes; stronger
owner-session pressure then exposes pager receive exhaustion as a fatal service
exit. The existing atomic pager-status word now hands temporary import pressure
to the control thread and waits through its reclaimer with a futex. Only that
thread retires cache owners. Other pager failures remain fatal and diagnosed.
The fixture verifies recovery and dirty persistence before resuming admission.
Two fresh full39 runs plus three pressure/session rounds and48 publication
cases each pass recoveries, private maps, shutdown and fsck. One earlier run
still had two close failures. A deterministic core RED separately proves that
bounded sync deferral incorrectly becomes a saved close error. Bounded BUSY
now remains a continuation result; actual backing conflicts and I/O errors
still enter the saved-error ledger. Tests distinguish both paths. Dirty VM
pins continue to protect pending bytes. The final coherent host/sanitizer/target
and native all39 plus three pressure/session rounds,48 publication cases,
recoveries, private maps, shutdown/fsck gates pass. DE25 trial A passes native
private maps, pressure and both recoveries but reaches its guard before final
shutdown; that failed trial is archived and verified before a shorter second
trial uses the identical immutable product. B passes real handle/session
pressure, shared/private mappings, final publication and clean shutdown88.
All four observers return zero. The success archive verifies574 hashes,
manifest, settings/key, unchanged boot/selector and fsck before cleanup.
The failed first trial is separately archived and verified. Selected services
return to fresh stage8 with zero restarts, loop0 only and8.6GiB free; user data
and selected release are unchanged. This qualifies the native private-view
prerequisite, not the remaining POSIX/EOF/UI/media/release contract.
Earlier intermittent failures are retained, not treated as passes.
Earlier derived-session ACCESS can be an expected denied security probe; that
trace alone does not establish the cause. This native view
mechanism does not complete POSIX writable/shared/anonymous maps or full
EOF/truncate semantics. Current state records source identity and interrupted
attempts.

Timestamp/error correctness68 is DE25 qualified and unreleased. Accepted write time is
persisted through the existing checked page writer before fsync. A descriptor
timestamp callback uses the original retained writer, never a reopened path.
The confined Linux provider applies explicit times to its retained writable FD;
QEMU cannot do this directly on root-owned files. Private protocol16 updates
all producers/consumers together and keeps native public record sizes intact.
Metadata failure leaves the VM snapshot dirty and preserves retry state. Full
successful flush clears the logical overlay so later stat/retirement report
the same stored timestamp, including filesystem precision. Explicit native
timestamp updates serialize on the same node and flush pending bytes first.
HostFS BUSY conflicts preserve a saved non-BUSY writeback error; an initial
conflict still records BUSY. Haiku's inode write-stat transaction supplies the
ordering pattern. Full host/sanitizer, application/normal-ROM and NDK
distribution/CMake/docs checks pass. Matching QEMU/provider binaries compile
for host and DE25; the existing root observer verifies QEMU UID1000 and resource
caps. Native all39, old client control, timestamp persistence, both recovery
checks, shutdown88 and read-only fsck pass in84.0s. DE25 passes the old control,
new timestamp/cache/exhaustion/prefix probes, all nine cases with pressure before
rooted-grant creation, both recoveries, repeated publication/root checks and
shutdown88 in583/600s. All four observers return zero. The transferred archive
verifies576 hashes, exact release manifest, paired settings/key, unchanged Linux
boot/selector and read-only fsck before private-state cleanup. Fresh selected
stage8, three services with zero restarts, only loop0 and8.5GiB free pass afterward.
Selected user data is unchanged; remaining mapping/UI/media gates block release.

Production integration67 is DE25 qualified and unreleased. Ordinary HostFS reads/writes
and bounded sync now share VM page owners. Native metadata changes must flush
the affected cached nodes while retaining their existing I/O owners; dirty
pages cannot be discarded to accept a changed ctime/link count. Root and
directory-relative operations share validation and dispatch. The new
nine-operation regression and existing million-operation core model pass;
full filesystem correctness work remains open. Current state records
failed experiments, exact source backups and the captured native timeout.
The retained sync budget now shares one remaining-scan pointer across owners
and chunks; NULL means full flush. Exhaustion cannot wait on an active owner.
Full shutdown flushes all owners before retirement. Namespace admission reuses
the existing I/O wait/wake cell, keeping path resolution, cache flush and
metadata mutation together while native data operations use their node owner.
Unit fixtures cover these paths, dirty-target replacement and concurrent
namespace admission. Full VFS/HostFS/Supervisor sanitizer, coherent normal-ROM,
application and NDK gates pass. The native retry passes all39 cases, both
recovery checks, shutdown88 and fsck in73.3s, including grant-port pressure.
DE25 passes the old control, new probes, nine cases including pressure before
rooted-grant creation, both recoveries, repeated publication/root checks and
shutdown88 in528/600s; all four observers return zero. The transferred archive
verifies576 hashes, exact manifest, paired settings/key, unchanged boot/selector
and read-only fsck before private-state cleanup. Fresh selected stage8, zero
restarts, only loop0 and8.5GiB free pass afterward. Current state retains failed
experiments and exact source identities; the dirty-pin fixture also covers
same-EOF overwrite. Logical timestamp persistence and saved-error precedence
still need proof/fixes before completing filesystem correctness, followed by
writable/private/anonymous mappings, mapping-user attribution and shutdown UI.

Clean page publication66 (DE25 qualified, unreleased): a native
cache owner can fill an absent VM page through READ|ADMINISTER without waiting
on its own pager under node I/O. The additive Runtime call uses the existing
VM clean-publication primitive and resident-copy syscall validation/scratch.
It copies the checked prefix and zeroes the tail before publication, refuses
to overwrite resident or dirty pages and creates no dirty state or pager wait.
Haiku `cache/file_cache.cpp:340-410` guides partial-page preparation. Controlled
old-dispatcher RED, host/sanitizer and all 26 K1 markers pass; the old ROM is
rejected solely for the added guest marker. NDK distribution, CMake and public
documentation checks pass. Existing clean/dirty publication and syscall budgets
remain unchanged. Current state records exact source identity and the initial
Runtime mock whitelist failure. The actual native probe checks read-only owner
authority, the whole zero tail, no overwrite/dirty state and reclaim. All 39
native cases, both recovery checks, repeated root/publication, shutdown88 and
fsck pass. DE25 reaches stage8 at 69.1 seconds and passes the old control, new
probes, all nine cases including pressure, both recovery checks, repeated
publication/root checks and shutdown88 at 405/600 seconds. All four observers
return zero. The transferred archive verifies 576 hashes, exact release
manifest, paired settings/key, unchanged boot/selector and read-only fsck before
private trial cleanup. Fresh selected stage8, zero service restarts, only loop0
and 8.5 GiB free pass afterward. Current state records exact identities and the
transient staging SSH observer failures. Selected user data is unchanged.
Production canonical HostFS read/write/sync and bounded worker integration
remain unfinished; implement that complete path next using the existing owners.

Mapping admission65 (DE25 qualified, unreleased): production
FILE_AREA now uses a typed native creation callback under the existing node
I/O fence, outside the state mutex. Metadata is refreshed after waiting and
READ authority is checked before publishing a view. Pager sources duplicate
their retained description rather than reopen a path; their backing grant
and native I/O pointer remain live through client close/session death.
Pressure maintenance skips active nodes before link-stat/retirement work.
Invalidated exact owners are reclaimed and revalidated under that fence,
preserving the same canonical area. Haiku `fs/fd.cpp:141-202,283-321` and
`cache/file_cache.cpp:760-825` guide locked references and serialization.
The old reclaim policy fails the active-node regression. Host/native/NDK
gates pass after correcting the focused stale-owner reuse regression; current
state records exact source/snapshot identities. DE25 passes all nine cases,
including physical cache pressure, both recovery checks, another publication/
root/root check and shutdown88 in 393/600 seconds; all four observers return
zero. The transferred archive verifies 576 hashes, exact release manifest,
paired settings/key, unchanged boot/selector and read-only fsck before private
trial cleanup. Fresh selected stage8, zero restarts, only loop0 and 8.5 GiB
free are confirmed afterward. Selected user data remain unchanged. Ordinary
HostFS cached writes and the bounded production worker remain unfinished.

Bounded shared writeback64 (DE25 qualified, unreleased): the
file-pager module now owns the reusable dirty-scan/snapshot/backing-write/
completion loop. Caller retains the area, rooted writer and existing node
I/O owner across the entire operation. Scan calls consume the remaining
budget. A resumed scan reaching the end is not a clean claim; only a complete
clean scan from zero succeeds. Redirty and in-flight pages remain pending;
failed or short writes preserve VM dirty data. Each page uses the updated
checked backing revision and logical EOF. Private cache-sync and retained
sync share zero/full versus nonzero/bounded semantics, with BUSY preserving
the saved backing error. Public sync and truncate require full completion;
close reports saved errors while retained owners keep pending data.
Haiku `vm/vm_page_writer.cpp:176-282` guides snapshot retention and redirty.
Host/sanitizer and NDK gates pass; the native probe replaces its one-page
writer with this helper, refuses to report a one-step flush as full sync, and
passes all 39 cases, recovery, shutdown and fsck. DE25 passes the old control,
new read/shared-writer and exhaustion/prefix probes, eight cases, both recovery
checks, repeated publication/root checks and shutdown88; all four observers
return zero. The transferred archive verifies all 64 hashes, exact release
manifest, paired settings/key, unchanged boot/selector and read-only fsck before
private-state cleanup. A corrected observer filename then confirms fresh
selected stage8, zero service restarts, only loop0 and 8.5 GiB free. Current
state records exact identities and the retained failed invocation. Production
HostFS canonical ownership and worker integration remain open.

Native read ownership63 (host/native/NDK verified; hardware coverage in64): ordinary and
retained reads can use the same byte owner as cached writes, under the existing
node I/O fence. Missing read support for attached metadata fails closed rather
than returning stale backing bytes. READ_PATH now uses a charged native
description and retains rooted authority through all short reads and session
death. Its EOF/capacity snapshot is refreshed after waiting for node ownership,
preventing old size from being paired with a newly completed writer's bytes.
File stat, retained stat and whole-file metadata use one shared operation;
backend, cache and wire byte-result validation also share one predicate.
Haiku `cache/file_cache.cpp:515-713,760-825` supplies the common byte-owner and
serialization patterns. Three host regressions first fail; final host,
sanitizer and NDK checks pass. Actual native VM bytes and logical EOF are
visible before backing flush through normal/whole/sequential reads; all 39
cases, recovery, repeated root/publication checks, shutdown and fsck pass.
Current state records source/snapshot identities and the corrected mock stat
fixture. This does not yet enable production HostFS cached writes or bounded
worker flushes. Checkpoint64 supplies the combined hardware coverage.

Lifetime fixes61/62 (DE25 qualified, unreleased): native file-token
and session-ID exhaustion now returns LIMIT rather than aliasing an old
identifier. Retiring a file token leaves its shared I/O cell usable. Existing
sessions and files remain live. Haiku `fs/fd.cpp:141-202,283-355` guides locked
removal and reference lifetime; Astra retains its native stale-token contract.
The first complete native run then exposes a shared POSIX mutex handoff race:
the old owner clears metadata after native release and erases the next owner's
data. A captured physical mutex word and blocked threads establish the cause.
The fix follows Haiku `posix/pthread/pthread_mutex.cpp:156-184`, clearing owner
metadata before release and retaining it on failure. Deterministic old/new
host regressions, namespaced host ASan/UBSan, all POSIX tests and NDK examples
pass. Native retry changes only libc in the same input, then passes all 39
cases, 64 extra publication races, recovery, shutdown and fsck. This fixes the
shared adapter, not a file-area-specific workaround. No kernel or ABI change
is needed. DE25 passes the new exhaustion/cache sync/scan probes, eight cases,
both recovery checks, repeated publication/root checks and shutdown88; all
four observers return zero. The transferred archive verifies all 64 hashes,
paired private settings/key, unchanged boot/selector and read-only fsck before
trial cleanup. Selected runtime is healthy with zero restarts and 8.5 GiB free;
its user data is unchanged. Current state records exact sources, the rejected
stall and corrected harness failures. Production cached writes, writable
mappings and promotion of this qualified candidate remain open.

Dirty discovery60 (host/K1/native/NDK verified; hardware pending): one bounded
query reads existing sparse VM dirty metadata, distinguishing modified,
in-flight and redirtied pages. Zero state returns a continuation/end cursor;
it is not permission to infer that earlier pages are clean. Reclaim shares
the traversal and still completes pending-page preflight before eviction.
Haiku `vm/VMCache.cpp:1287-1335` provides the owned-page iteration pattern.
Host/sanitizer and all 25 K1 markers pass; worst scan is 2,947/25,000 cycles.
Reclaim remains within its unchanged budget. NDK distribution/CMake checks
pass after documenting the new flags. Native Runtime callers prove failed
snapshot retention, retry and clean end; all 38 cases, recovery, shutdown and
fsck pass. Production write/worker integration and DE25 proof remain open;
see current state for exact artifacts, initial SDK documentation failure and
the newly identified file-generation rollover hazard.

Flush ownership59 (host/native verified; hardware pending): one private cache
ops table supplies write and sync. Native sync/truncate hold the existing node
I/O owner while the callback flushes outside the state mutex. Positioned page
writeback may explicitly borrow that owner only through a retained WRITE
description of the same node. Failed flush/sync preserves ownership and is
reported on both client-close paths; successful retry and truncate update the
shared logical/backing metadata. Host/sanitizer tests and real native VM-page
sync through an independently opened read-only handle pass, together with all
38 native cases, recovery, shutdown and fsck. Production HostFS write/sync
integration and DE25 proof remain unfinished. Next, expose bounded dirty-page
discovery from the existing VM metadata, without a userspace dirty ledger or
treating snapshot INVALID_ARGUMENT as proof of cleanliness.

Shared logical metadata58 (DE25 qualified, unreleased): the existing
node I/O owner anchors optional caller-owned metadata and its rooted writer.
Path/file stat, lookup, open and SEEK_END share logical EOF and mutation time;
raw backing revision remains the writeback validation source. Sparse holes
are zero-filled only within checked logical EOF. Failed writeback preserves
its error and writer; pending sync/truncate cannot claim durability or discard
data. The caller must prove VM pages clean and exclude external writable
mappings before retirement checks sequence, backing EOF and saved error, then
detaches and clears caller storage. No new byte store, lock or node index is
introduced. Haiku
`fs/vfs.cpp:4796-4866` provides the optional attachment pattern. Embedding the
138-byte metadata in every possible file slot caused native root-grant resource
refusals; those attempts are preserved. Optional storage reduces the MC68040
file record from 276 to 144 bytes and restores repeated root access without
raising limits. Host/sanitizer and all 38 native cases, old/new cache probes,
partial-I/O, both recovery checks, shutdown 88 and read-only fsck pass.
DE25 passes old/new probes, eight cases, both recovery checks, repeated
publication and clean shutdown 88 with all four observers returning zero.
A separate root/writeback/root follow-up also passes; its stale-console
startup-observer race and later redundant QMP reset remain recorded as harness
failures, not second-startup proof. Both archives verify their hashes, paired
settings/key, unchanged Linux boot/selector and read-only fsck. Private trial
files are removed after verification; selected services are healthy with zero
restarts and the board has 8.5 GiB free.
Production HostFS does not attach this metadata yet. Ordinary cached writes,
sync/dirty-owner integration and writable mappings remain unfinished; see the
current-state map for the exact evidence and hardware qualification status.

HostFS write boundary57 (DE25 qualified, unreleased): ordinary writes
now use the existing native writer hook, including its pinned rooted WRITE
reference and shared node I/O owner. Their invalidation stays on that front
end; the backing operations use the direct provider writer so page writeback
no longer enters the synchronous invalidation wrapper. Haiku
`cache/file_cache.cpp:515-713` supplies
the cache/front-end versus `vfs_write_pages` separation. The old backend
operations fail the new resident-area regression. Host/sanitizer gates and
all 38 native cases, recovery checks, shutdown 88 and read-only fsck pass.
DE25 passes the old control, owner/prefix probes, eight filesystem cases,
both recovery checks, repeated publication and shutdown 88; all four observers
return zero. The sealed archive verifies all 65 hashes, paired settings/key,
unchanged boot/selector and read-only fsck. Selected runtime reaches fresh
stage 8 with zero restarts. Private trial files are removed after verified
transfer, leaving 8.5 GiB free on the board.
This does not yet place ordinary bytes in VM-owned cache pages or enable
logical metadata and dirty writeback; see the current-state map for evidence.

Confirmed I/O prefixes56 (DE25 qualified, unreleased): inline and
shared-area reads/writes retain confirmed byte counts and sequential positions
alongside an operation error. Failed transports or invalid replies clear
reusable state and confirm no bytes; this does not prove a backing write had
no effects. Native File cursor tracking consumes a confirmed prefix, while
POSIX already returns a short count. Borrowed error reads are not cached as
successful results. Shared reply helpers validate counts and positions; the
existing private service pump is split from the canonical client transport so
the native probe can run a real service without embedding a second client.
Haiku `cache/file_cache.cpp:810-825` supplies the partial-I/O pattern. Host and
sanitizer gates pass; old public/port paths fail the new behavioral assertions.
All 38 native cases, recovery checks, shutdown 88 and read-only fsck pass.
DE25 passes the new prefix probe, old control, cache-owner probe, eight
filesystem cases, both recovery checks, repeated publication and shutdown 88;
all four observers return zero. The sealed archive verifies all 65 hashes,
paired private settings/key, unchanged boot/selector and read-only fsck.
Selected runtime reaches fresh stage 8 with zero restarts. Private trial state
is removed only after verified transfer, leaving 8.5 GiB free on the board.
This repairs the client boundary prerequisite; it does not enable production
cached writes or writable mappings. See the current-state map for evidence.

Native cache-writer handoff55 (DE25 qualified, unreleased):
ordinary and retained writes now share an optional private owner callback.
VFS pins the original rooted WRITE description while holding the existing
node I/O owner, then calls outside the state lock. The callback may adopt that
reference; otherwise the normal retained-release implementation consumes it,
including close during the write and final-close errors. Backing page writes
remain cache-bypassing. Read-only callers gain no authority. Host and sanitizer
tests cover adoption, borrowing, rooted recovery, quota and error paths.
The native probe copies ordinary write bytes into existing VM-owned pages,
retires its prior reference, and flushes through the cache-held writer while
preserving untouched bytes, EOF and cursor state. All 37 native cases, both
recovery checks, clean shutdown 88 and read-only fsck pass. DE25 passes the
old control, new cache-page probe, eight cases, both recovery checks, repeated
map publication and shutdown 88; all four observers return zero. The sealed
archive verifies 65 hashes, paired settings/key and read-only fsck before
private-state cleanup. Haiku `cache/file_cache.cpp:760-940`
and `fs/fd.cpp:188-240` provide the single-owner and retained-description
patterns. No new byte cache, lock, public ABI or kernel mechanism is added.
Only the native probe installs the callback so far; production HostFS must
still join this seam to its canonical file areas and shared logical metadata.
An additional public-client RED shows that inline/shared-area reply and client
handling discard an accepted prefix when write status is an error. Preserve
both count and status through that boundary before enabling multi-page cache
writes; private core ownership tests alone cannot qualify the client contract.

Logical EOF backing-writeback prerequisite54 (DE25 qualified, unreleased):
the existing private page writer takes cached logical EOF independently of
the expected backing revision. An empty backing file, partial last page and
sparse later page can grow without changing cursor/append state or accepting
a stale revision. A smaller logical EOF does not truncate backing bytes.
Short-write failures retain their validated accepted prefix and checked
metadata; failed writeback never authorizes cleaning the VM snapshot.
All callers and tests use the same private signature. No public ABI, wire,
kernel or RTL change, additional lock or byte store is introduced.
Haiku `cache/file_cache.cpp:515-713,760-910` and
`vm/vm_page_writer.cpp:176-282` supply the partial-page, EOF, short-I/O and
snapshot-failure patterns. Host/sanitizer gates pass. The expanded native
probe reuses the existing page reader for complete readback rather than
assuming a scalar read exceeds its 208-byte transport limit. The coherent
native run passes the preserved old control, expanded EOF case, all 37 cases,
both recovery checks, shutdown 88 and read-only fsck in 71.7 seconds.
DE25 passes the old control, expanded EOF case, eight cases, both recovery
checks, repeated map publication and shutdown 88; all four observers return
zero. The sealed archive verifies 65 hashes and read-only fsck. See the
current-state map for identities and retained failed-attempt evidence.
Ordinary cached writes and cache-owned logical EOF remain unimplemented.

Shutdown follow-up53 (DE25 qualified, unreleased): an early trace captures
HostFS declining shutdown with IO after all filesystem/recovery cases pass.
A deterministic regression proves that normal last-sender departure poisons
the shared VFS receive pump's fatal state. The shared fix preserves genuine
receive failures while accepting drained peers; three coherent native runs
pass all 37 cases, both recovery checks, shutdown 88 and read-only fsck.
DE25 also passes its eight-case/recovery/map-publication/shutdown gate; all
four observers return zero and the sealed archive verifies 67 hashes and
read-only fsck. See the current-state map for artifact and evidence identities.
Cleanup-stage diagnostics and early bounded shutdown observation retain the
next failure's context. The original checkpoint52 timeout lacks that early
state, so its exact branch is not retroactively proven by later passing runs.
This does not implement ordinary cached writes.

Node I/O ownership prerequisite52 (DE25 qualified, unreleased): exact regular
files in one mount incarnation join the existing native I/O owner across
independent opens. This reuses alias exclusion, sequence fencing, wait/wake
and caller-funded cell lifetime; it does not merge provider cursors or flags.
Rooted backend bindings supply their immutable claim incarnation. OPEN and
OPEN_AT acquire that owner before truncation, sharing the ordinary truncate
implementation and removing duplicate HostFS open wrappers. A host regression
reproduces the independent-open stale-fill race before the change; host,
ASan/UBSan and ThreadSanitizer suites pass afterward. The final claim-epoch
target build passes. Native run B passes all 37 cases, reset/replacement,
shutdown 88 and read-only fsck in 52.2 seconds. Run A passed the cases but timed
out on shutdown; its cause remains unproven. The harness now requires the
existing answer-plus-prompt readiness boundary before returning a command.
DE25 passes eight cases, reset/replacement/fresh I/O, repeat map publication
and shutdown 88; all four observers return zero. Startup takes 63.2 seconds
and native qualification reports 287/600 seconds. The sealed archive verifies
64 hashes, paired settings/key and read-only fsck, with unchanged boot/selector.
There is no second node mutex or
new byte cache. Ordinary cached writes remain unimplemented.

Partial-write backing-read prerequisite51 (DE25 qualified, unreleased):
the existing native page reader and pager source accept explicit READ or WRITE
access. WRITE-only cache preparation uses the backend's private
`read_for_write`; ordinary client READ and FILE_AREA access remain unchanged.
RAM and ext4 reuse their readers. HostFS's internal positioned, single-page
request requires WRITE and rejects read-only sessions, cursor-based requests
and cross-page ranges. The rooted provider reopens only its already-confined
regular-file descriptor through procfd, without replaying create/truncate.
Rename/unlink/replacement preserve inode identity. A failed read-capability
upgrade preserves valid write-only access and reports UNSUPPORTED for cache
preparation. Retained lifetime, activity and sequence fences remain shared.
Haiku `cache/file_cache.cpp:515-713` and `fs/vfs.cpp:4735-4760` provide the
boundary-page preparation and backing-vnode access patterns. Host/sanitizer,
RAM, ext4 and real-provider protocol gates pass. The coherent native image
passes all 37 cases, reset/replacement, shutdown 88 and read-only fsck in
49.6 seconds; its page-writeback case also covers checkpoint50's runtime
publication wrapper through the installed DSO. This is not ordinary cached
write integration. DE25 passes all five real-provider protocol scenarios in
62.3 seconds, the rooted-path test as an unprivileged user, eight native cases,
provider reset, HostFS replacement/fresh I/O, repeat map publication and clean
shutdown 88. All four observers return zero; startup takes 66.7 seconds and
the native phase reports 292/600 seconds. The sealed archive verifies 76 hashes,
paired private settings/key and read-only fsck. A whole-image allocation in
the initial staging preflight caused Linux OOM; the corrected preflight streams
checksums under a 48 MiB limit, and protocol QEMU runs alone under 256 MiB.
The selected user-data image separately passes offline read-only fsck with
unchanged before/after checksums, then boots freshly with zero restarts.
Evidence:
`/home/barry/astra-evidence/cache-backing-read-20261113/`.

Page-read lifetime prerequisite45 (DE25 qualified, unreleased):
positioned page reads borrow the existing retained description instead of its
exclusive cursor/I/O acquisition. Public close detaches a retained description
without waiting on active I/O; final retained release still waits before
backing close. Existing activity/sequence checks reject a fill overlapping
description I/O, including sync without a byte mutation. Cache preparation
must precede the exclusive write phase. No new lock, owner table, public ABI,
kernel or VM state is introduced. Haiku `src/system/kernel/fs/fd.cpp:188-240`
guides retained lifetime; `cache/file_cache.cpp:760-925` releases its cache
lock around pinned I/O. Host, ASan/UBSan and ThreadSanitizer suites pass.
Native QEMU passes seven cases, provider reset, HostFS replacement, shutdown
diagnostics, clean shutdown 88 and offline fsck in 50.2 seconds. Candidate
`7eadaee22eccd05238fe611ad681f3168c200cf16cb2503629f2f6d769e0b258`
changes only HostFS/fssem in the disposable image. Evidence:
`/home/barry/astra-evidence/vfs-page-lifetime-20261112/`.
DE25 passes the same seven cases, provider reset, HostFS death/replacement,
fresh I/O, exact shutdown diagnostics and clean exit 88. All four gate
outcomes are zero; startup reaches stage 8 in 63.5 seconds and private runtime
takes 363 seconds within its unchanged 600-second guard. The sealed archive
verifies 57 hashes, paired private settings/key and read-only fsck; Linux boot
identity is unchanged across guest shutdown. Archive SHA256 is
`ed38bceaf349b0ef30a5d8c806bb26ceec3ccc5971e79a7195b1319a962b53fc`.
Private board files are removed after verified transfer; the unchanged
selected release reaches stage 8 with all three services active, zero
restarts, loop0 only and 8.4 GiB free.
This does not connect ordinary writes to cached pages or enable writable maps.

Writeback access prerequisite44 (DE25 qualified, unreleased): the existing native
retainer accepts explicit nonzero READ/WRITE access. Retained references keep
the original description's access, while each retained read/write checks its
own permission before backing I/O. This permits write-only writeback ownership
without a read-authority shortcut. FILE_AREA requests still require READ.
Full VFS/HostFS host and ASan/UBSan suites pass after correcting the new
fixture's backend context. The bounded job verifies its cgroup limits before
compilation. Native QEMU and DE25 pass write-only retention/read refusal,
positioned snapshot writeback, provider reset, HostFS death/replacement and
fresh I/O, exact shutdown-error reporting and clean shutdown. Host offline
fsck passes. Candidate
`946e144a4d3b043ed340579008cfd176758fe868194384012262bd96682f4d91`
changes only HostFS/fssem in the disposable image; shared library, catalog,
kernel and ROM remain unchanged. DE25 startup reaches stage 8 in 60.9 seconds;
all four gate outcomes are zero within the existing 600-second guard.
The sealed hardware archive verifies 57 hashes, paired private settings/key
and read-only fsck; Linux boot identity is unchanged across guest shutdown.
Evidence and source hashes:
`/home/barry/astra-evidence/vfs-write-access-20261112/`.
This does not connect ordinary writes to cached pages
or enable writable mappings. Haiku `src/system/kernel/fs/fd.cpp:188-240`
guides retained-description lifetime independent of descriptor closure.

Native ownership migration, 2026-11-12 (unreleased, qualification incomplete):
wire 42 and Filesystem Kit ABI 18 remove the direct provider client adapter.
All ordinary I/O and FILE_AREA use authenticated native sessions/descriptions.
Cache sources retain the existing native file slot; retirement and provider
loss have distinct semantics. Release consumes an in/out token exactly once,
including when final backend close fails. Native holder attribution joins the
existing provider inventory and notification sequence; exact mapping holders
remain open. Native ISO requests borrow their authenticated pending share
before obtaining an independent provider image lease. Root recovery rebinds
path sessions while old descriptions retain their lost backend. No second
cursor, token table or ownership ledger is introduced. The current state map
records qualification progress; historical guarded-DATA descriptions below
describe their checkpoint, not this unqualified worktree.

Retained snapshot writeback qualified on DE25, 2026-11-11: the native service exposes
its existing positioned snapshot writer through retained file ownership.
One acquisition spans backing validation, all short writes, sync and final
stat, with the service state lock released during I/O. Access checks precede
backing I/O. Validated accepted bytes advance the native mutation sequence
even when the rest fails; errors leave VM completion to the owner as failure.
The expected/current metadata arguments may alias. The cache owner still
serializes node-wide mutations and reconciles external changes. No new cache, cursor,
lock, worker, public export or ABI is introduced.

Haiku `src/system/kernel/cache/file_cache.cpp:1453-1502` routes backing writes
through its cache owner, and `src/system/kernel/fs/fd.cpp:188-240` keeps open
descriptions alive through use. Astra composes its existing retained file
acquisition and snapshot writer rather than another byte-I/O path.
Host tests, ASan/UBSan and ThreadSanitizer pass; 10 ownership/error mutations
are rejected. Native scoped-io now writes a snapshot after client close,
backend rebind and session reuse, preserving the original root, EOF four,
append flag and cursor 123. Beast passes 10 cases, provider reset, two fresh
I/O cases and fsck. Candidate
`faf21cfd3e008ece227f41b4596090b79de7af3601ecdc41e79117974f324e03`
changes only fssem; its private-core integration uses the existing extra-source
seam. Kernel, ROM, runtime libraries and services are inherited unchanged.
DE25 passes the same 10 cases, provider reset and two fresh I/O cases.
Startup observer, native gate and 600-second trial all return zero without
panic/degraded markers. All 561 archived hashes match, including the offline
image and paired private settings/key; read-only fsck passes. Final source
patch matches the candidate. Private trial state is removed; selected
services are restored at stage 8 with zero restarts, loop0 only and 8.4 GiB
free. Production image/data were not opened by trial tooling. Artifact
transfer kept root ownership and reused 2,628 immutable files through link-dest.
Initial fixture/build/launch/mutation failures
are retained in `/home/barry/astra-evidence/vfs-retained-writeback-20261111/`.
This does not enable grant DATA or writable maps: canonical file-area ownership,
cached mutation, dirty conflict/writeback and exact mapped-user gates remain.

Native description transfer, 2026-11-11: SHARE reserves an existing native
file slot and pins its captured backend before provider I/O. Pending one-use
tokens remain charged to the authenticated source session. ADOPT requires
the same grant and backend incarnation, reserves the target owner's quota
before I/O and transfers the slot in place, including at table capacity.
Only the sharer may withdraw a pending token. Failed adoption or withdrawal
retains ownership for retry; closing-session authority is revoked immediately
and the existing port reaper retries failed cleanup after reply-lane retirement.
OPEN and SHARE reuse reservation/accounting; direct and service adapters share
transfer request validation. There is no second cursor or token table.

Haiku `src/system/kernel/fs/fd.cpp:188-240,364-390` retains underlying open
descriptions separately from descriptor slots. Astra adapts that lifetime
through its existing generation-checked table and scoped backend references.
Review found a race where an in-flight token appeared absent even though a
failed backend adoption could restore it. Explicit TRANSFERRING state now
returns BUSY after authentication; source charge remains and failed target
reservation rolls back. Both the failing assertion and final tests are kept.
Full VFS/HostFS host and ASan/UBSan gates, Supervisor tests and coherent target
builds pass; all 13 injected ownership/rollback regressions are rejected.
ThreadSanitizer also passes the million-operation model and port suite after
making two shared host fake-backend counters atomic. The initial race report
is retained; that fixture change affects no product or native test binary.
Beast passes 16 native cases, provider reset, two fresh I/O cases and fsck.
The native scoped-io probe drives concurrent claim/withdraw and failed claim
retry against real provider descriptions, submitting provider work only on
the borrowed channel's owning thread. Kernel BIN/ELF remain byte-identical;
public protocol/ABI stay unchanged. DE25 passes 10 native cases, provider
reset and two fresh I/O cases; startup observer, native gate and 600-second
trial return zero without panic/degraded markers. Candidate:
`a47dc5b95e1e6f924798789f48dd14b1bb7a18132027a8e5f9a0d5073891ebbd`.
All 560 archived hashes match, including the offline trial image and paired
private settings/key; read-only fsck passes. Final product/native source
matches the candidate; only host fixture counter atomics differ. Private
trial state is removed. Selected services are restored at stage 8 with zero
restarts, loop0 only and 8.4 GiB free. Production image/data were not opened
by trial tooling.
Evidence: `/home/barry/astra-evidence/vfs-native-shares-20261111/`.

At that checkpoint Grant DATA remains guarded and FILE_AREA still consumes a
direct provider token; the native ownership migration above removes that path
and joins authenticated retained-description ownership. Cached mutation/writeback, dirty-page
conflicts, writable/private mappings and exact mapped-user attribution remain
required. Do not add a raw-token fallback or a second provider owner ledger.

Grant admission pressure, 2026-11-11: idle read-cache areas retained HostFS
handles until scope creation occupied slot 254; creating the next grant port
needed two slots and returned RESOURCE_LIMIT. Grant scope/port admission now
uses the existing idle-area reclaimer, with one bounded retry only for
resource pressure. Session open, file adoption and area creation share that
retry policy. Handles, mappings, child references and pager pins still
prevent eviction; cleanup failures remain owned and retryable. No quota,
handle capacity, ABI or kernel change is introduced.

Haiku `src/system/kernel/port.cpp:977-1007` accounts port admission, and
`src/system/kernel/cache/file_cache.cpp:296-340` reclaims clean unmapped cache
pages under pressure. Astra applies that pattern to its existing native area
references rather than adding another resource budget or cache mechanism.
Nine host pressure cases and ASan/UBSan pass; eight retry/admission/ownership
mutations are rejected. Beast passes 22 native cases, provider reset, two
fresh I/O cases and fsck. DE25 passes 11 cases, provider reset and two fresh
I/O cases; observer, native gate and 600-second trial all return zero without
panic/degraded markers. Orphan tests retain the eight-grant workload, now
release their transfer mappings/handles and require precise loss/peer-death
rejection. Exact-view tests obtain their own free address range. Original
fixture failures and the HELLO peer-death diagnostic are retained.
Candidate `7a4921b337695e99a5979110449658a7d5d22b93a9f4a7429d11363ab6b97e50`
replaces only HostFS and fssem; libraries, kernel and ROM remain unchanged.
All 563 archived hashes match, including the trial image and paired private
settings/key; read-only fsck passes. Native/test patch matches the candidate.
Trial state is removed. Selected services are restored at stage 8 with zero
restarts, loop0 only and 8.5 GiB free. Production image/data were not opened
by trial tooling. Grant data admission remains guarded; this closes the
resource-pressure gate, not cached mutation or mapped-user attribution.
Evidence:
`/home/barry/astra-evidence/vfs-grant-admission-20261111/`.

Service description controls, 2026-11-11: SEEK, FILE_FLAGS and
POSITION_ADVANCE now pass through the existing service handle/session
checks and file acquisition before reaching the backend. The direct adapter
uses the same dispatch. Cursor position and mutable status flags stay in
the provider description; the service does not copy either state. Unknown
flags, attempted access-mode changes, unused fields and offset overflow are
refused before backend I/O. Successful replies publish only checked positions,
status flags and conditional-advance outcomes. One shared predicate now
validates conditional cursor advancement in the backend, dispatch and cached
read client, replacing duplicate comparisons.

Haiku `src/system/kernel/fs/fd.cpp:934-950` pins the descriptor while seeking;
`src/system/kernel/fs/vfs.cpp:6417-6445` limits mutable flags to append and
nonblocking state. Astra keeps its existing generation-checked acquisition
and provider-owned cursor/status flags rather than another descriptor copy.
No public API/export, ABI, protocol, lock, worker or kernel/ROM change is
introduced. Share/adopt still need scoped requester/root authority before
ordinary I/O can move behind the canonical cache owner.

Full VFS target/host and ASan/UBSan gates pass. The service test covers pins,
negative seek deltas, malformed results, mutable flag masks, conditional
commit/mismatch, overflow, stale/cross-session handles and missing callbacks.
Four private mutations losing pins, seek-result bounds, input masks or advance
result checks are rejected. The existing native description-io case now also
seeks and resets a real provider cursor, commits and loses conditional advances,
and sets/clears append flags before the sequential I/O checks. Beast passes
that case, shared-description, append-race, page-writeback, file-map and basic,
then provider fsck. DE25 passes all six plus cache-pressure, area-range,
durable-save, exec-inherit and alias-identity through native Terminal. Startup
heartbeat advances before stage 8; observer, native gate and 600-second trial
all exit zero without panic/degraded markers. Candidate release is
`47978c243d66d43f1fd922b31b1d00eba173dcef39a231d6e59cd479f0fa4b26`.
Archive hashes, paired private settings/key and read-only fsck pass. The final
native/test patch exactly matches the deployed candidate. Private trial state
is removed; selected services return to stage 8 with zero restarts, loop0 only
and 8.5 GiB free. Production data was not opened by trial tooling. Fixture
write-limit and Kit provider-selection mistakes are retained in evidence;
product validation was not relaxed. Evidence:
`/home/barry/astra-evidence/vfs-description-controls-20261111/`.

Service description I/O, 2026-11-11: the generic VFS service now accepts the
existing sequential read/write flags and returns the backend's resulting
position for inline and shared-area reads. Positioned and retained reads
still leave the shared cursor unchanged. It reuses
`astra_vfs_backend_read_position`; the superseded private bulk-read API and
its callers are replaced together. No public ABI, protocol, second cursor,
worker or lock is added. The backend remains the sole description-position
and append owner. This repairs an owner-path prerequisite, not canonical
cache mutation ownership or writable file mapping support.

Haiku `src/system/kernel/fs/fd.cpp:715-845` distinguishes sequential from
positioned I/O, and `src/system/kernel/cache/file_cache.cpp:760-920` joins
cache I/O at one shared boundary. Astra adopts those responsibilities while
retaining its existing provider-owned cursor and atomic append implementation;
it does not copy Haiku's separate descriptor-position bookkeeping. Malformed
bulk requests now return INVALID without a redundant inline dispatch. Owner
checks already prevented that dispatch from reading bytes; the regression
checks both unchanged backend state and unchanged dispatch accounting.

Full VFS host/MC68040 and ASan/UBSan gates pass. Focused service and port
tests cover sequential position, positioned independence, bounded append,
EOF, malformed results, unsupported backends and invalid bulk requests.
Four private mutations restoring sequential read/write refusals, dropping
bulk position or restoring redundant bulk dispatch are rejected. The fourth
initially escaped byte-I/O-only assertions; explicit dispatch accounting now
detects it, with both logs kept.
The native `fssem description-io` fixture compiles the exact private core
and existing path/UTF-8 sources through the existing extra-source seam, then
uses real provider descriptions through client close and retained reads.
Beast passes this case plus page-writeback, basic, shared-description,
file-map and cache-pressure, followed by provider fsck. Only Filesystem Kit,
HostFS, Events and fssem are replaced in the disposable image; kernel and
ROM stay unchanged. DE25 passes all six cases plus area-range, durable-save,
exec-inherit and alias-identity through native Terminal, then completes its
600-second trial without panic/degraded markers. Observer, native gate and
trial all exit zero. The archive includes paired private settings/key,
passes hash verification and read-only fsck, and has matching native source
sections; the final patch adds only port-test accounting assertions. Private
trial state is removed; selected services are restored at stage 8 with zero
restarts, loop0 only and 8.5 GiB free. No production image or user data was
opened by trial tooling. Candidate release is
`439ad3e0ce8ae0c32eed609d94a680c8571741d30af0cf8284cad94806bfa752`.
Evidence:
`/home/barry/astra-evidence/vfs-description-io-20261111/`.

Native writable pager views, 2026-11-11: area mapping now admits an authorized
WRITE view. Pager pages start protected in both creation orders (publication
before or after the first map). The existing fault path checks the mapping's
write permission before requesting a missing page or marking a resident page
dirty. It uses the mapping's source offset, existing dirty metadata and shared
page protection; read-only peers stay protected. Snapshot sealing traps later
CPU stores again. Resident kernel output uses the same process preparation
path, so a syscall cannot bypass dirty tracking. That path does not perform
nonresident pager I/O. Public HostFS file-area capabilities remain READ|MAP;
ordinary cached writes and writable/private file maps are still incomplete.

Haiku `src/system/kernel/vm/vm.cpp:4035-4118` checks access before fault work,
and `vm/vm_page_writer.cpp:176-282` keeps later modifications separate from
the snapshot being written. Astra retains those ordering rules through its
existing area fault, user-copy, snapshot and completion mechanisms. Internal
callers carry decoded write intent; no public protocol or ABI changes. Host
user-copy wrappers now exercise process preparation rather than compiling it
out. Area/process tests cover both creation orders, offset views, narrowed
rights, allocation failure, snapshot redirty and protected kernel output.
Eight independent authority/protection/offset/dispatch/copy mutations fail.

Normal qualification exposed sparse reclaim walking every reserved page for
each invalidation. The preceding candidate also hits the short diagnostic
deadline; samples from both land in the same sparse-page walk. Haiku
`src/system/kernel/vm/VMCache.cpp:1287-1335` iterates resident cache pages.
Astra reuses its existing sparse page and dirty directories: absent leaves
are skipped, empty areas stop immediately, and pending-page preflight still
finishes before any frame is removed. The area lifetime test now puts its
second page at the maximum-area boundary and checks whole-range refusal.
The existing K1 fixture adds full-span populated and empty reclaim with an
unchanged 125,000-cycle area-reclaim budget: measured baseline 948,523 cycles
fails; retained implementation takes 19,633 cycles on Beast and DE25. No new
iterator, cache, worker, accounting store or assembly path is introduced.
Boundary tests also reject three mutations losing pending-page refusal or
skipping the first resident/dirty page after an absent metadata leaf.

Full kernel host/MC68040, area/process ASan/UBSan and NDK distribution gates
pass. Beast and DE25 pass 23 K1 markers, including actual user stores and
syscall output into protected pager memory, with zero overruns. Beast also
passes native page-writeback/basic/file-map/cache-pressure and provider fsck.
DE25 also passes nine native cases, including the 512-file pressure sequence,
shared descriptions, area ranges, durable saves, exec inheritance and alias
identity. Its 600-second private trial passes without panic/degraded markers.
Archive hashes match the image and migrated private settings/key; read-only
fsck passes. Selected services return to stage 8 with zero restarts, private
trial files are removed and board free space returns to 8.5 GiB. Production
data was not opened by trial tooling. This qualifies the native prerequisite,
not canonical VFS mutation/writeback ownership. Evidence and failed diagnostics:
`/home/barry/astra-evidence/pager-write-fault-20261111/`.

Positioned snapshot backing I/O, 2026-11-11: the existing VFS backend seam
now writes a sealed page snapshot without touching the open description's
cursor or append state. It rejects stale identity/revision before mutation,
joins short positioned writes, trims the final page to the captured EOF,
syncs before success and returns checked post-I/O metadata even after a
partial write or sync failure. Zero progress and malformed byte/position
results fail. Only a successful, reconciled result permits the owner to
complete its native VM token successfully; failures retain dirty state.
The caller must retain and serialize the node and reconcile external changes.
This helper is service-private and does not route ordinary writes through
the cache or establish global append/cursor ownership.

Haiku `src/system/kernel/cache/file_cache.cpp:760-850,1453-1502` provides
the positioned cache-I/O and partial-progress reference; its
`src/system/kernel/vm/vm_page_writer.cpp:176-282` separates writeback from
later modifications. Astra reuses its backend operations, pager worker,
native area lifecycle and existing direct-client lock. The native
`fssem page-writeback` case drives a real 4,300-byte file through page-in,
owner modification, snapshot, backing sync, completion, clean reclaim and
refault. Its append-mode description stays at offset 123 and EOF stays 4,300.
The integration command compiles the exact service-private implementation
through its existing extra-source build seam; no new public Kit export,
parallel byte store, cursor or worker is introduced.

Full VFS host/MC68040 and ASan/UBSan gates pass, including full-page short
writes, tail clipping, stale metadata, malformed results, partial failure,
sync failure and post-I/O identity/size changes. Four private mutations
removing EOF, sync, revision or error handling fail the existing core test.
Beast passes native page-writeback, basic and file-map plus provider fsck.
DE25 passes those cases plus shared-description, area-range and durable-save,
then a 600-second private stability trial. The paired image/settings archive
hashes match and read-only fsck passes. This is scoped correctness evidence;
ordinary cached writes, writable mappings and performance qualification are
not complete. Evidence: `/home/barry/astra-evidence/page-writeback-20261111/`.

Pager-owner I/O, 2026-11-11: the native lifecycle now has owner-authorized
resident modification, snapshot and completion syscalls and Runtime/NDK
wrappers. ADMINISTER alone cannot read or write page bytes; those operations
also require READ or WRITE respectively. Each transfer stays within one page
and reuses the existing resident-read buffer, range checks and user-copy
mechanism. Invalid source copying precedes mutation. Failed snapshot output
is completed as failure before returning BAD_ADDRESS, so the owner can retry.
The anonymous and pager write paths share their byte-copy implementation.
The existing K1 user-mode pager fixture exercises this API, including full
4 KiB modification/snapshot, mapped visibility, failed output rollback,
redirty, failed completion and clean reclaim. The normal writable-mapping
guard stays until the complete VFS write path owns coherence and backing I/O.
New syscall cost uses the existing 50,000-cycle dispatch budget; native
dirty/copy/snapshot budgets remain unchanged. This adds no second cursor,
cache, lock, worker, provider or byte-storage backend.
Host process/runtime and full kernel host/MC68040 suites pass. Process
ASan/UBSan passes; four independent authority/source-copy/rollback mutations
fail at the native process seam. The complete 4 KiB user-mode fixture passes
all 20 K1 markers on Beast and DE25, syscall maximum 4,602/50,000 cycles
(baseline 3,005), zero overruns. NDK dist-check passes with additive ABI-3
Runtime exports. Board evidence hashes match, private files are removed and
selected services return to stage 8. Normal artifacts are restored. VFS
backing-write and cursor/append ordering remain the next integration boundary;
this does not qualify writable file mappings or whole-filesystem performance.

Dirty-page prerequisite, 2026-11-10: reviewed Haiku
`src/system/kernel/vm/vm_page_writer.cpp:176-282` and
`src/system/kernel/vm/VMCache.cpp:1288-1344`. Modified and in-flight state
must be separate: snapshotting clears only the modified flag, later edits
set it again, and failed completion restores it. Astra permits one snapshot
per page, consumes only its exact nonwrapping token, and keeps pending pages
under one existing area child reference. Token exhaustion refuses further
dirty marks or snapshots without wrapping or discarding data. Clean pages remain reclaimable;
whole-range invalidation first refuses any dirty or in-flight page without
partially discarding the range. Clean-page reclamation does not invalidate
unrelated writeback tokens. Lazy directory/leaves use the existing tagged
page-metadata allocator; the opaque area record grows four MC68040 bytes,
with eight records still fitting one frame. No new queue or worker exists.
The native area seam covers redirty, failure, duplicate/stale completion,
allocation rollback, sparse metadata, lifetime and pressure. File writes and
writable user mappings remain guarded until VFS coherence/writeback is joined.

Before the new path, the existing K1 area fixture copied a complete resident
page 32 times: Beast's pinned MC68040/icount baseline is 3,322 cycles, with
all 18 markers passing. The copy budget is 5,000 cycles; new dirty-mark,
snapshot and completion budgets are 5,000/6,000/256 cycles respectively,
assigned before their target gate. Snapshot cost includes sealing and the
same existing copy. This is a dependency regression gate, not filesystem
performance qualification. Evidence lives in
`/home/barry/astra-evidence/dirty-page-lifecycle-20261110/` on Beast.
Full kernel host/MC68040 verification, area ASan/UBSan and six independent
broken-behavior mutations pass. Beast and DE25 K1 both pass all 19 markers:
dirty mark 577/5,000, snapshot 3,465/6,000 and completion 100/256 cycles,
zero overruns. The 32-round fixture checks immutable snapshots, redirty,
failure/retry, stale completion and pending-page invalidation before clean
pressure reclaim. Hardware log/ROM hashes match their archive; private board
state is removed, selected services/stage 8 and 8.5 GiB free are verified.
Normal artifacts are restored. This is the native lifecycle prerequisite;
backend writeback, VFS write coherence and writable mapping gates remain open.

Checked shared-page protection prerequisite, 2026-11-10: VM can update a
current owned page's write-protection bit without unmapping or replacing its
frame. Expected-frame validation refuses stale transitions; other descriptor
bits and read-only attachments survive. Shared-table metadata validation is
centralized, including explicit alignment (generic frame queries round down).
The existing VM test covers rights, stale/corrupt inputs, flush/no-op behavior
and teardown. Four independent broken-behavior mutations fail. Full kernel
host/MC68040 verification and Beast/DE25 K1 pass: 17 markers, 43/128 cycles
against a 54-cycle clear/set baseline, no overruns. Verified hardware evidence
is archived; private board state is removed, selected services/stage 8 and
8.5 GiB free verified. Haiku references and identities are in CURRENT_STATE.
Pager-area writes remain refused; dirty state and writeback must precede use.

Startup liveness, 2026-11-10: accepted, changing boot-service progress now
feeds the existing watchdog before the supervisor enters its normal watch
loop. Invalid or unchanged reports cannot keep it alive. The 120/60-second
deadlines are unchanged. Kernel tests/verification, runtime-watchdog tests,
NDK distribution checks and Beast/DE25 K1 (16 markers, zero overruns) pass.
A clean 600-second DE25 startup/stability trial passes; its verified archive
passes read-only fsck. An isolated repeat also passes ten native cases,
Atlas/Terminal, provider loss/reopen and basic/file-map after reset, observing
heartbeat before stage 8. Its verified archive passes fsck. A final repeat
with both rebuilt shared libraries and matching provider metadata passed all
ten cases and 600 seconds. Recovery hit an old fixture directory (EEXIST);
the harness now uses fresh probe paths. Two consecutive resets, visible
loss/reopen and basic/file-map after each pass on the same binaries in a
400-second trial. Trial/observer/recovery gates all returned zero, with no
panic/degraded markers. Final archive hashes match and read-only fsck passes;
private board state is removed, all three selected services are active at
stage 8, only production loop0 remains and 8.5 GiB is free. No product change
or image rebuild was needed for the fixture repair.
Tests use a separate private QMP listener: the first observer repeat
monopolized the watchdog listener and is diagnostic only. Startup speed remains unqualified.
The private page-read profile is complete and archived; its five guarded
operations account for about 12.8 seconds over 1,536 pages. No probe remains.
CURRENT_STATE.md records the Haiku reference, identities and latest gate.

Rejected query-batching experiment, 2026-11-10: independent metadata/counter
samples shared a host submission on each side of page I/O. All identity,
revision, short-read and mutation-counter checks were retained. Host,
sanitizer, target, NDK and native recovery gates passed, but the clean DE25
trial still failed startup watchdog at 141 seconds. The optional callback
and scoped-batch API are removed; evidence is retained. Two independently
proven existing transport defects (ring growth and returned offsets) remain
fixed. No runtime parser limit or watchdog deadline was increased. Periodic
maintenance peaked at 187 ms, below its one-second cadence; that scheduling
hypothesis is unsupported. A private eight-poll variant passed native recovery
but still missed DE25 startup watchdog at 144 seconds, so the 512-poll default
is retained. The completed page-in profile is recorded above.

Publication reconciliation, 2026-11-10: notification delivery no longer
invalidates pages independently after the cache observer reconciles the same
event. Both acquisition and publication use file_areas_observe_nodes() and
its watermark; genuinely newer events still invalidate, and snapshot failure
remains fail-closed. A focused test fails before this repair and passes after
it, including broker BUSY retry, covered GAP, new-node delivery, and snapshot
failure. Host/sanitizer/target builds pass. A ten-case native sequence,
provider-reset recovery and fsck pass; DE25 acceptance remains pending.
The native residency probe pins the original owner and tolerates only a
proven concurrent node-generation advance. A private always-missing mutation
is rejected; stable-generation misses remain failures.

Maintenance admission, 2026-11-10: stable retained owners no longer incur
reference queries when neither retirement nor a link check can collect them.
Discard, retiring grants, stale epochs, closed sources, unknown identities,
missing links and check_links still query exact kernel references. Pins,
mappings and children still prevent collection; failed cleanup stays charged.
A focused cost test fails before the change and passes afterward, with the
existing retirement tests and sanitizer/target builds. The updated native
ten-case gate, provider recovery and fsck pass, but the clean DE25 retry still
misses startup watchdog at 142 seconds. All three failed/diagnostic images
pass read-only fsck and are archived with verified checksums. Board trial
state is removed and selected services restored. Hardware qualification is
still required; the watchdog deadline remains unchanged.

Control admission, 2026-11-10: HELLO scope duplication uses the existing
negotiated cache reclamation once on RESOURCE_LIMIT, then retries once.
Ineffective reclaim returns LIMIT without creating a session. A failing
focused test now passes, and a native 512-file/512-descriptor workload plus
five fork/exec rounds passes with fsck. Storage protocol 15 scopes known
watcher GAP and repair to the exact volume incarnation; actual global loss
still invalidates all. Provider and QEMU tests cover both scopes and reject
malformed authority pairs. Original fanotify selection and peer filtering
are retained. No new cache, observer, or resource pool was added.

Watcher scope experiments, 2026-11-10: filesystem-wide fanotify on directory
exports caused unrelated host cleanup to invalidate all canonical pages when
deleted objects could no longer be scoped. Parent/metadata filters reduced
but did not eliminate this (13 WORK invalidations, 2,070 page requests,
30.667/31.699 ms cold/warm medians per 64 KiB); those filters are not retained.
An all-export recursive-inotify experiment passed isolated scope tests but
failed the full protocol oracle by duplicating Astra's own mutations; it is
not retained. Its 56.459/33.063 ms cold/warm medians and 2,087 requests remain
recorded. Scoped GAP/repair is retained, with real FID and forced-fallback
tests, ASan/UBSan, and the full QEMU protocol oracle. Unknown changes still
produce GAP. CURRENT_STATE.md records evidence and the Haiku scope reference.

Earlier qualification checkpoint, 2026-11-10: retained owners alone did not
remove the read regression. Existing metrics now expose per-volume cache
hits, creates, requests, invalidations and retirement reasons. A native
diagnostic showed one WORK owner and 127 hits, but 194 invalidations and
2,113 page requests. The Linux provider turned directory metadata events
without a separate target FID into global GAP records. Its parser now uses
the directory's DFID_NAME/DFID identity and emits a nameless exact NODE
record. A provider-boundary inside/outside directory chmod test fails before
the repair and passes afterward; full provider and sanitizer suites pass.
Fresh guest/counter and hardware gates remain required. The final retained
fixture also exposed child filesystem reinitialization failing with EMFILE
in exec-inherit (five of six cases passed); control admission above repairs
that failure. No new runtime is certified. Kernel dispatch repair is pushed
as b42d1533. Exact evidence,
failed measurements, Haiku references and provider documentation are recorded
in CURRENT_STATE.md.

Sequential reads and owner retention WIP, 2026-11-10: one shared reader now
serves positioned, sequential scalar and sequential bulk requests. Sequential
reads conditionally advance the native shared description after copying,
retrying a cursor race within the pager deadline. Actual copied bytes alone
advance position. Host cap POSITION_ADVANCE and its VFS feature make this an
additive protocol operation, with no public layout or export change.
The full guest sequence exposed a kernel fault-to-idle dispatch defect;
RESUME and IDLE are now distinct, and every assembly entry decodes the same
target contract. Kernel tests and Beast/DE25 K1 pass, as do six native guest
cases and provider-reset recovery. The first combined hardware runtime
failed its startup watchdog because the cache reader was roughly 100 times
slower. That failed experiment is retained, not certified. Its maintenance
closed idle canonical owners before the next ordinary read could reuse them.
Retention and explicit best-effort CACHE_RECLAIM admission are under test.
Pinned, mapped, child-owned and externally held areas cannot be collected;
failed cleanup remains charged. The client retries resource-consuming direct
admission once, and only when the grant advertises reclaim. HostFS also
reclaims before retrying session creation and before removal/usage/shutdown.
No public object layout changed. Host/sanitizer tests pass; new native
retention/quota probes, baseline comparison and a fresh DE25 trial remain
required. CURRENT_STATE.md records evidence paths and Haiku references.

Borrowed and whole-path cache reads, 2026-11-10: file-area providers now use
the same positioned reader for borrowed source reads and whole-file reads.
The lane transfer area remains a temporary output buffer, not another file
cache. During pager waits it belongs to the call so disconnect cannot unmap
it; no inflight guard is held across operations that may refresh the client.
On success the existing lane takes ownership again. Repeated reads reuse
the same transfer allocation and consult canonical pages again. Whole-path
reads open one retained description and close it on every outcome, including
empty files, size refusal, and I/O failure. Existing non-file-area providers
retain their native transfer path. No public ABI layout or export changed.
VFS host tests, ASan/UBSan, target builds, library contracts, and NDK
dist-check pass. A coherent Beast guest passed six native cases and
basic/file-map after provider reset, with read-only e2fsck. The file-map
case proves both public borrowed-source and whole-file reads populate the
original canonical area after separate writes invalidate it. Host tests also
disconnect during prefetch and require no dangling output or mapped-area
leak. Coherent private DE25 release
`61e358e881d869178cd73628e5ad0df517f6aa22bdc0fbe4184ec424b93859f9`
passed all six native cases, visible provider loss/reopen, and basic/file-map
after reset (2/2). It remained at stage 8 for 600 seconds with no
panic/degraded markers and restored the selected runtime with result 0.
Image/console hashes matched archived Beast copies and read-only e2fsck
passed. Verified trial state was removed; all three services are active,
only production loop0 remains, and 8.6 GiB is free. Exact evidence identities
are recorded in CURRENT_STATE.md. Haiku
`src/system/kernel/cache/file_cache.cpp:760-910` keeps the caller buffer and
cache reference alive across waits; Astra applies that ownership rule to its
existing borrowed transfer buffer.

Covered notification replay, 2026-11-10: after bounded drain forces a full
invalidation, the reconciliation watermark must not move backward when
older queued records arrive. HostFS now ignores NODE records covered by that
watermark and avoids another drain-limit invalidation for the same snapshot.
Newer records still invalidate, including those newer than the caller's
snapshot; GAP and enumeration errors remain fail-closed. A lower fresh
snapshot after provider restart/rollover invalidates all pages and resets
the cursor for the new stream. A focused test failed on old code and passes
with the fix, along with HostFS ASan/UBSan, MC68040 build, coherent Beast
six-case gate, and basic/file-map after provider reset. A 600-second DE25
private trial of the final HostFS binary passed six native cases, visible
loss/reopen identity checks, and basic/file-map after each of two resets.
The stopped disposable image received only the HostFS update through existing
installer helpers; the verified base runtime and selected production data
were preserved. Post-trial image/console hashes matched board and Beast,
read-only e2fsck passed, and verified trial state was removed. All three
services recovered, only production loop0 remains, and 8.6 GiB is free.
Exact base, overlay, image, and evidence identities are in CURRENT_STATE.md.
Diagnostic-only hardware run passed six cases plus six additional
file-map cases without the observer fix; the earlier intermittent residency
failure remains unclassified. The diagnostic does not weaken that assertion.
Haiku `src/system/kernel/cache/file_cache.cpp:760-910` and
`src/system/kernel/fs/node_monitor.cpp:716-764` guided retained cache identity
and exact-node notification routing. Astra extends its existing reconciliation
state rather than adding another event or cache owner.

Shared client reads, 2026-11-09: positioned scalar and bulk reads now use
one Filesystem Kit adapter over canonical `FILE_AREA`, `AREA_PREFETCH`, and
resident copy. The service-only read adapter has been deleted. Misses use the
existing pager without a private mapping, so failed ordinary I/O cannot kill
the client with a mapped fault. The adapter closes its area handles, bounds
repeated eviction retries by the pager deadline, clamps concurrent shrink,
and preserves partial reads. Read permission is checked both at the native
wrapper and at the service trust boundary. Runtime exports now include the
two native area I/O wrappers. Exact identity/chunk reuse survives revision
changes: HostFS reclaims and refreshes the existing owner instead of creating
a competing owner alongside older mappings. VFS/HostFS host and sanitizer
gates, target builds, and NDK `dist-check` passed. Coherent Beast guest
`fssem file-map` proves that `pread` populates a page observed through the
original canonical handle after growth; it also reads across a sparse 128 MiB
boundary. `fssem area-range` passed. Private DE25 release
`48fcb42da30a7ecbdb665ed04204e042978dd842920edc4b29228761dd753963`
ran 300 seconds at stage 8; native Terminal passed both cases, 2/2, while
Atlas enumerated Applications. Post-trial image/console hashes matched
archived Beast copies; read-only `e2fsck` passed. Selected runtime and user
data were preserved. The broader shared-description gate initially stalled on the
candidate and on the prior image; the fork control-session repair below
resolves it.
Haiku `src/system/kernel/cache/file_cache.cpp:760-910` guided common cache
ownership, waiting for busy pages, and partial-I/O reporting. Sequential,
borrowed, and path read shapes remain open; this does not claim cache
retention across independent ordinary reads without a surviving area user.

Forked cache users, 2026-11-09: fork now registers the child's own HostFS
control session through ordinary HELLO while preserving its inherited
provider scope and shared file descriptions. A provider session ID is not a
control session ID: the old detached-client path sent mutation notifications
under an unbound child/session and waited forever for the refused request.
That bypass and private state byte are deleted. Client layout remains
unchanged on MC68040. VFS host/sanitizer tests, target builds, NDK
`dist-check`, and coherent Beast guest shared-description, exec-inherit,
file-map, area-range, basic, and durable-save gates pass. The file-map gate
now requires a child's write to invalidate the parent's mapped page.
DE25 private release
`3112da5a7c4556fa703ba505e77c4956e0e29029e7651109e745e56d732e6ea0`
ran 240 seconds at stage 8. Native Terminal passed five cases, including
shared-description and exec-inherit. The file-map case passed forked-write
invalidation but later failed its immediate resident-copy proof after an
ordinary read: combined suite is 5/6, not certified. Split the compound
assertion before attributing that failure to I/O, ownership, or allowed page
eviction. Pending node-event ordering also needs inspection. Haiku
`src/system/kernel/fs/vfs.cpp:5026-5094` guided separate child I/O context
ownership with retained shared file descriptions; Astra reuses its existing
control registration and provider scope.

Handle-based cache fill, 2026-11-09: `AREA_PREFETCH` requests a page through
the existing native pager lifecycle using a READ handle, without a user
mapping. The same retained request builder serves mapped faults and handle
I/O. A failed backing read reports a syscall error rather than killing the
caller. Final kernel/runtime host tests, NDK `dist-check`, and all 16 K1
markers on Beast/DE25 passed with zero overruns. DE25 syscall dispatch max
was 2,994/50,000 cycles. Checksummed ROM/log evidence is archived under
`/home/barry/astra-evidence/file-cache-prefetch-20261109/`; disposable board
harness state was removed and normal build products restored.
Haiku `src/system/kernel/cache/file_cache.cpp:760-910`
waits on busy vnode-cache pages; Astra reuses its native pager waits and frame
owner. This is a prerequisite for the shared client read adapter.

Coverage correction: the resident ordinary-read service adapter below is not
on production direct-lane application reads. The DE25 `pread` check proved
semantics, not reuse of resident pages. Production grant ports are
control-only; route real client reads through the existing canonical file
area and the native pager, with I/O errors reported to the caller. Positioned,
sequential, bulk, and borrowed reads must converge there before claiming
ordinary-I/O cache integration.

Resident ordinary-read slice, 2026-11-08: native area syscall 120 copies from
one resident page through READ authority, returning WOULD_BLOCK on an absent
page without triggering a pager fault in HostFS. HostFS ordinary reads consult
the exact-node area only while its provider generation, node identity, and
revision remain stable; cache misses and changed files use the original
backend read. Kernel, runtime, and HostFS host tests and HostFS sanitizer
pass. Beast target kernel/HostFS builds, NDK `dist-check`, K1 QEMU, and
coherent Beast guest `fssem file-map` passed. Private DE25 release
`ab446f5ee4de4727b35ce37c0f96667be582d43d33415afd7c6a1b2576f35172`
ran 300 seconds at stage 8; native `fssem file-map` and `fssem area-range`
each passed 1/1. Post-trial image and console hashes matched board and Beast;
offline read-only `e2fsck` passed. Evidence is under
`/home/barry/astra-evidence/file-cache-read-20261108/`. Trial files were
removed, selected release and production data preserved. USB replug was not
performed. A follow-up native `fssem file-map` checks ordinary read after
mapped fault; coherent Beast guest and a second 240-second DE25 trial passed
1/1. Image and console hashes matched archived copies, offline `e2fsck`
passed, and board trial state was removed. This
is a read-hit slice, not the full ordinary-I/O or writable-cache contract.
Haiku `src/system/kernel/cache/file_cache.cpp:745-850` guided resident-page
reuse under vnode ownership; Astra preserves its userspace HostFS owner and
native area frames.

Canonical file-area WIP, 2026-11-08: storage protocol 41 and
`filesystem.library.17` return a fixed 128 MiB owner chunk for each requested
window. Overlapping requests for one exact node share that owner. Read-only
POSIX `mmap` maps only requested views and joins adjacent owners at exact
virtual addresses; repeated maps remain independent. Beast host tests,
MC68040 builds, NDK `dist-check`, and coherent QEMU `fssem file-map` and
`area-range` passed, including a sparse 128 MiB boundary test. Private DE25
release `3a283a79b1b630faf932b21f9d3d325b75bb476fb354b1f1b950f848dc0dd371`
ran 300 seconds at stage 8; native Terminal passed both gates 1/1. Trial image
passed read-only `e2fsck`; image/console hashes matched archived Beast copies.
Evidence is under `/home/barry/astra-evidence/file-cache-canon-20261108/`.
Selected release and production data stayed intact; board trial state was
removed. Linux saw no attached USB device, so USB remount remains untested.
Ordinary VFS I/O still bypasses these pages; remaining acceptance
work below is unchanged.

Native area subrange mapping, 2026-11-08: WIP kernel now records a source
offset and view length for each mapping and attaches only corresponding
existing shared tables. New syscall and runtime export require 256 KiB source
alignment and full 256 KiB views except for a terminal partial span. Host tests
cover distinct and overlapping views of one area, decommit/refault, terminal
bounds, permissions, and process dispatch. Exact-address mapping uses the
existing syscall's fifth input; zero retains automatic placement. Host tests
cover adjacent 256 KiB exact views, collision, and shared frames. MC68040
kernel/runtime builds, NDK `dist-check`, and 16-marker K1 QEMU gate passed
without overruns. A coherent Beast guest and a 300-second DE25 private trial
passed native `fssem area-range` at 256 KiB granularity with exact adjacent
placement, collision rejection, shared bytes, decommit, and refault. The
trial also passed existing `fssem file-map`, read-only image `e2fsck`, and
physical USB mounted-state check. Evidence is under
`/home/barry/astra-evidence/file-cache-exact-20261108/` on Beast. K1 does not
yet sample the new exact-address path; its existing area-map budget applies.
HostFS now uses canonical chunks; ordinary VFS I/O still bypasses mapped pages. Haiku
`src/system/kernel/cache/file_cache.cpp:745-850`
uses one vnode VMCache for file I/O and mappings; Astra reuses `KernelArea`.

Exact change retirement, 2026-11-08: HostFS now has a cache cursor separate
from reliable broker delivery and drains the existing per-namespace change
ring before area reuse. NODE invalidates matching inode areas, while
incomplete identities, GAP, missing records, and bounded-drain overflow stay
conservative. The previous generation observer invalidated every mapped file
before reading exact records. Beast HostFS test, ASan/UBSan, and MC68040 build
passed. A disposable Beast guest passed `fssem file-map` and `volume-catalog`
with provider `e2fsck`. Coherent release
`b844ae906bf8131a5ccce531f2056a624abe1ab3ef1df5beb4701357b34d9ce5`
passed a 300-second private DE25 trial at stage 8. Native `fssem file-map`
passed 1/1, and `disk list` showed both USB partitions mounted read/write.
Post-trial image SHA-256
`9d2dde8587424593c1b71582365bb7a712adef98d79ebe9e5fc8dd62aa2f79ce`
matched board and Beast and passed read-only `e2fsck`. Trial console had no
panic or degraded marker. Selected release and production data were preserved;
trial files were removed after evidence archiving at
`/home/barry/astra-evidence/file-cache-node-invalidation-20261108/`. Haiku
`src/system/kernel/cache/file_cache.cpp:760-820` keeps identity and I/O
ordering under one cache owner; Astra uses its existing HostFS area owner and
change stream, without a second notification path.

Incomplete-identity mappings, 2026-11-07: HostFS now permits a retained
description to back a read-only file area even when the provider cannot report
the full volume/inode/birth-generation identity. Such areas never share across
opens. Exact-node notifications and local mutations conservatively invalidate
them; the existing provider revision checks still guard each page read.
Focused HostFS and VFS pager tests, HostFS ASan/UBSan, and MC68040 HostFS/VFS
builds passed on Beast. A disposable Beast guest passed `fssem file-map` and
`volume-catalog`, followed by provider `e2fsck`. A guarded DE25 private trial
reached stage 8; native Terminal reported `FSSEM DONE passed 1 failed 0` for
`fssem file-map`, and `disk list` showed both USB partitions mounted read/write.
The trial image passed read-only `e2fsck`; selected release and production
data were preserved. Checked console and RFB evidence is under
`/home/barry/astra-evidence/file-cache-unknown-identity-de25-20261108/` on
Beast. Trial state was removed from DE25 after verification.

One intervening trial boot panicked when HostFS could not find its settings:
the first boot migrated configuration from the image into private host state,
then the next trial copied only the image into a new empty host-state root.
That root received the migration marker without the moved settings. The same
post-boot image reproduced `hostfs status=2` on Beast. Pairing the image with
its private `state/system` directory fixed the reboot; the final DE25 trial
passed. Keep the two together for any repeated private trial.
Haiku `src/system/kernel/cache/file_cache.cpp:1273-1327` attaches a VM cache to
a vnode; Astra keeps the provider description as the safe owner when no
stable cross-open vnode identity exists.

Immediate mutation coherence, 2026-11-07: DE25 exposed a correctness gap
after `pwrite`/`fsync` and `ftruncate`/`fsync`. An already-faulted mapping can
still return the old byte immediately after each operation returns; the
existing test's four-second wait hid this. The strengthened `fssem file-map`
gate now checks both return boundaries. Red RFB evidence is under
`/home/barry/astra-evidence/file-cache-immediate-20261107/` on Beast.
HostFS already owns exact file-area identities, area reclaim, and provider
node-change notifications. Canonical guest writes and truncates must revoke
affected resident pages before their VFS reply; the existing notification
path remains responsible for external changes and application updates. Haiku
`src/system/kernel/cache/file_cache.cpp:763-1000` holds its vnode cache while
ordering ordinary I/O and mapped-page access. Astra must adapt that ordering
to its userspace VFS service and kernel area owner.
DE25 pre-change `fsbench` measured 1,024 random 4 KiB operations per mode:
read median/p95 31,156/60,126 MC68040 cycles, write 32,260/68,657 cycles.
The `--cleanup` option removed its fixture directory after reporting, and
the report was removed after an RFB capture. Post-change DE25 `fssem file-map`
passed immediate write, truncate, and extension/refault. `rand-read` and
`rand-write` each completed 1,024 operations without failures. Random read
median/p95 rose to 38,961/125,209 cycles; write rose to
736,354/1,519,407 cycles. The unconditional grant-control mutation exchange
costs too much when no mapping exists, so this remains an unselected
correctness trial. The first bounded reclaim retry left an ambiguous outcome
when a write had committed but an in-flight pager fault blocked revocation.
The WIP branch now resolves that race at the native area owner: reclaim
advances an epoch even with outstanding child references, and stale pager
replies retry the fault without publishing old data. HostFS removed its
one-second post-commit retry. Beast kernel and HostFS host tests, MC68040
builds, and 16-marker QEMU K1 passed; pager reply maximum moved from 2,981
to 2,993 cycles against its 75,000-cycle budget. Prepared Beast guest
`fssem file-map` and `volume-catalog` passed with provider `e2fsck`. A
five-minute private DE25 trial reached stage 8, passed native `fssem file-map`
and `disk list` for both USB partitions, and its image passed read-only
`e2fsck`. Selected release and production data were preserved; checked RFB
and console evidence is under
`/home/barry/astra-evidence/file-cache-epoch-20261108/` on Beast. The
unconditional write-path performance regression remains open.
Native direct and accelerated bulk operations plus ordinary service calls all
reach the existing HostFS file-area owner. Do not reintroduce a separate cache
or an asynchronous-only local write path.

Read-only POSIX file mapping, 2026-11-07: `libc.library` now adapts
`mmap` with `PROT_READ` and either `MAP_SHARED` or `MAP_PRIVATE` to the
existing native file-area and area-map APIs. It closes the temporary area
handle after mapping and tracks
only mapping address and span for `munmap`; file pages, invalidation, and
reclaim stay with the HostFS/VFS native owners. Exact rounded-span unmap works;
partial unmap, writable or anonymous maps, and fixed placement report
`ENOTSUP`. DE25 `fssem file-map` passed both read-only modes, fd close before
fault, partial-unmap rejection, native same-size invalidation, truncate, and
refault. The first hardware attempt exposed an unaligned MC68040 static mutex
word; aligning it to four bytes fixed `ASTRA_SYSCALL_INVALID_ARGUMENT`.
Checked RFB evidence is under
`/home/barry/astra-evidence/posix-mmap-de25-20261107/` on Beast. Haiku
`src/system/libroot/posix/sys/mman.cpp:85-153` adapts POSIX calls to native
areas, and `src/system/kernel/cache/file_cache.cpp:1418-1490` owns shared
file pages; Astra kept that layering without adding a second cache.

Acquisition freshness, 2026-11-07: Existing provider node-change generation
is now sampled when a file area is requested, before area reuse. This closes
the interval between a completed same-size write and the once-per-second
change poll; invalidation still uses the one existing page-reclaim path.
Host test failed before the change, then passed with ASan/UBSan and MC68040
build. Native DE25 `fssem file-map` now acquires a second mapping immediately
after a same-size write and checks both new and existing mappings, then
truncate and refault. It passed; checked capture is under
`/home/barry/astra-evidence/file-area-freshness-20261107/` on Beast. Haiku
`src/system/kernel/cache/file_cache.cpp:760-820` holds its cache lock while
satisfying I/O; Astra checks the provider generation before shared area
lookup, using its existing HostFS change source.

Provider recovery, 2026-11-07: Existing HostFS change invalidation now runs
on provider-epoch transition before grant sessions reopen. A file area's
provider epoch also participates in the reuse key, so successful reclaim
cannot make an old area eligible again when its stale flag clears. Focused
host test, ASan/UBSan, MC68040 build, targeted DE25 install, native mapped
file checks before and after two QMP provider resets passed. The hardware
gate did not hold a mapped client across the reset. Evidence is under
`/home/barry/astra-evidence/file-cache-provider-epoch-20261107/` on Beast.
Haiku `src/system/kernel/cache/file_cache.cpp:1314-1327` and
`src/system/kernel/fs/vfs.cpp:990-1020` guided cache ownership at teardown;
Astra uses its existing epoch and pager-area lifecycle.

Cross-grant read-only sharing, 2026-11-07: HostFS now keys reuse by live volume
owner, complete node identity, revision, and file window, rather than grant
pointer. Its first grant keeps the provider description through the area's
last mapping; a second authorized grant receives a handle to the same area.
The host test proves one bind for two grants and a separate bind for another
volume owner with the same reported metadata. The test failed before the
change, then passed with sanitizers. Targeted DE25 HostFS install and native
`fssem file-map` passed; checked RFB and binary evidence is under
`/home/barry/astra-evidence/file-cache-grant-share-20261107/` on Beast.
This does not route ordinary reads or writes through the cache. Haiku
`src/system/kernel/cache/file_cache.cpp:1418-1490` uses one vnode cache for
both ordinary and mapped I/O; Astra's volume owner and pager area are the
existing pieces for mapped-reader sharing.

Read-only pager pressure reclaim, 2026-11-07: kernel pressure worker runs a
bounded area sweep after idle library reclaim. It skips in-flight faults and
uses the existing shared-table withdrawal before freeing a resident frame;
mapped addresses remain and refault on demand. Beast host tests, Beast/DE25
K1 16-marker qualification, and a normal DE25 stage-8 boot passed. K1's
worst 64-step sweep measured 384/2,500,000 MC68040 cycles. Checked evidence:
`/home/barry/astra-evidence/file-cache-pressure-20261107/` on Beast. This
only covers clean read-only pager pages; writable shared mappings still need
dirty accounting and writeback. Haiku
`src/system/kernel/low_resource_manager.cpp:100-145` orders pressure
handlers, and `src/system/kernel/cache/file_cache.cpp` keeps backing file
identity separate from its mapped consumers. Astra reused its native worker
and area owners.

File-area revision and invalidation, 2026-11-07: HostFS no longer reuses a
mapped area merely because its node identity matches. It also compares the
reported size and available timestamps. Its maintenance reads the latest
provider node generation before draining the bounded broker queue and marks
file areas stale on a generation change, then reclaims their resident pages.
The broker remains the owner of application notifications; cache correctness
does not wait behind its publication backlog. A cold DE25 run exposed a
four-second invalidation timeout before this change. Three consecutive
native `fssem file-map` runs passed after it, including a cold first run.
The gate now uses the existing native `mkdtemp` for case directories and
removes its own file-map directory, avoiding stale-PID collisions. Beast host
tests, sanitizers, and target HostFS build passed; checked RFB evidence lives
at `/home/barry/astra-evidence/file-map-revision-de25-20261107/` on Beast.
This closes a mapped-area reuse and notification-backlog gap, but ordinary
reads still use the provider rather than resident mapped frames. Haiku
`src/system/kernel/cache/file_cache.cpp:345-430` ties cached pages to vnode
identity; Astra retains its existing provider generation and area reclaim
owners.

HostFS file-area integration, 2026-11-07: cache-owned exact-node windows use
one shared pager port and scratch page, retain the provider description and
grant session across client close, and retire only after area handle, mapping,
and in-flight fault counts allow it. Provider node/gap notifications mark
windows stale and trigger resident-page reclaim. Beast full guest and DE25
native `fssem file-map` passed two-page fault, close-before-map, and
write/invalidation/refault checks. A coherent ABI-3 DE25 private release held
stage 8 for 300 seconds; its post-trial image passed read-only `e2fsck`.
Checked evidence is under
`/home/barry/astra-evidence/file-map-de25-20261107/` on Beast. After the
matched private trial, release `a46dffcb6efa575f16f1ef074f8389f27c6f46faf1b58ca0aa9d335d48e3c6d1`
deployed through the state-preserving migration transaction; production
reached stage 8 with three services active, zero Astra restarts, and 8.5 GiB
free. The previous selected ROM used syscall ABI 2, which ruled out an
incremental ABI-3 HostFS install. Haiku `src/system/kernel/cache/file_cache.cpp:345-430` and
`vm/VMCache.cpp:1616-1644` guided backing-source and consumer lifetimes.

File-area control protocol, 2026-11-07: the existing grant port now accepts
one additive `FILE_AREA` operation. Client's direct lane makes a one-use share
token; the bound control session exchanges token plus page-aligned base/span
for one READ|MAP area handle. Server callback must own backing description and
keep its own area handle, returning only a transferable duplicate. Unknown
grant, absent feature, malformed request, and callback error cannot leak an
area handle. Beast VFS target, host and sanitizer gates, NDK distribution,
and static AArch64 fixture on DE25 passed. Checksummed evidence is under
`/home/barry/astra-evidence/vfs-file-area-port-final-20261107/` on Beast. HostFS
callback and actual guest fault are still open. Haiku
`src/system/kernel/cache/file_cache.cpp:386-427`
holds vnode-backed cache separately from mapped consumers.

File windows, 2026-11-07: a reserved area has a bounded span, while a file can
be larger. The shared page source now carries a page-aligned 64-bit file base;
kernel request offsets remain area-relative and the VFS reader receives
`base + offset`. Initialization rejects unaligned bases and overflow of the
full span. One pager port can therefore serve separate windows of one large
file. Beast target build, host suite, sanitizers, and static DE25 AArch64
fixture passed; SHA-256 evidence is under
`/home/barry/astra-evidence/vfs-file-window-20261107/` on Beast. Haiku
`src/system/kernel/cache/file_cache.cpp:345-375` also reads at
the requested file offset, independent of virtual mapping placement. HostFS
has not yet created or handed out any file windows.

VFS pager reply pump, 2026-11-07: the shared service-side pump validates
kernel page requests and answers from cache-owned backend descriptions via
one receive port, one scratch data area, and cookie lookup. This avoids a
wait-set entry per file. It uses the coherent one-page VFS reader. BUSY, LOST,
malformed request, short valid
byte count, and dead caller are covered by focused tests. Beast service
archive build, host suite, and ASan/UBSan passed; the static AArch64 fixture
passed on DE25 with matching binary/log SHA-256 under
`/home/barry/astra-evidence/vfs-file-pager-shared-20261107/` on Beast. The pump
acquires and releases each cache description around provider read and reply
send; it borrows the pager receive endpoint, mapped scratch page, and data
area. HostFS must own and retire those resources using area reference counts
and grant-session lifetime; it has not yet connected the pump to a guest file
mapping. Haiku `src/system/kernel/cache/file_cache.cpp:345-375,386-427`
performs vnode-backed fill and zero-tail; Astra uses VFS's coherent read and
the kernel's atomic zero-tailed publication.

Pager source pin, 2026-11-07: cookie lookup now acquires a cache-owner
reference and the pump releases it after the reply send. Even BUSY, invalid
span, peer death, and send failure retire exactly one acquired reference;
unknown cookies acquire none. This prevents concurrent cache retirement from
closing a description during provider read. Haiku
`src/system/kernel/vm/VMCache.cpp:1616-1644` drops the store reference after
consumer removal and outside the cache lock; HostFS must use the same ordering
when its worker and grant retirement run concurrently.

Transient source mutation, 2026-11-07: a valid pager reply with BUSY now
retries the same fault through the existing bounded kernel pager path, keeping
its original 30-second deadline. The primary waiter keeps other same-page
waiters asleep until publication or terminal failure. K1 guest sends BUSY
before the first successful page and later refaults after reclaim; Beast and
DE25 passed all 15 markers with three pager replies and zero cycle overruns.
DE25 maxima were 1,291 request, 2,981 reply, and 79 handoff MC68040 cycles,
within existing budgets. Checked ROM and DE25 log are under
`/home/barry/astra-evidence/pager-busy-retry-20261107/` on Beast. This is
needed because the shared VFS backend page source returns BUSY if a file or
provider change sequence moves during fill. Haiku
`src/system/kernel/vm/VMCache.cpp:950-966,1563-1577` keeps page waiters behind
one publisher; Astra uses its existing port-call retry and waiter list.
No production VFS mapping consumes this yet.

Direct HostFS page source, 2026-11-07: production HostFS clients use provider
descriptions through the direct data lane, outside the VFS service slot table.
The existing coherent retained-page reader was factored into one shared
backend-description reader; the retained wrapper pins its slot across that
read. The direct path checks exact node identity, size, known timestamps,
and the existing provider node-change counter around the complete page.
Beast VFS target build, host suite, and sanitizers passed; matching AArch64
fixture passed one million forced switches on DE25. Binary SHA-256
`4afda974b39abc2af9b828693a76d537475edf5548baf79c9b8c2b08d452d0d0`;
checksummed evidence is under
`/home/barry/astra-evidence/vfs-direct-page-20261107/` on Beast. This shares
one source contract across service and direct backends; it does not yet own
areas, answer pager messages, or invalidate resident frames.

Area-lifetime prerequisite, 2026-11-07: an ADMINISTER-only native syscall
returns area handle, mapping, and in-flight fault counts in one kernel
section. A VFS cache owner can keep one area handle and retained file
description while clients map or fault, then retire both when its handle is
alone and the other counts reach zero. Guest K1 checks mapped and unmapped
counts; Beast and DE25 measured 42 and 43 MC68040 cycles against a
10,000-cycle budget. Host tests also cover capability checks, duplicated
handles, and cloned mapping retirement. Checksummed DE25 evidence is under
`/home/barry/astra-evidence/area-references-de25-20261107/` on Beast.
No VFS cache consumes the query yet. Haiku
`src/system/kernel/vm/VMCache.cpp:1616-1644` separates consumer and
backing-store references and releases the store outside its cache lock;
Astra applies that ordering across its native VFS service and kernel VM.

Call lifecycle prerequisite, 2026-11-06: failed `PORT_CALL` sends and reply
capability installation now abort their call number. Shared wait completion
also retires call numbers on deadline and cancellation before wake. This
prevents a later mutex wait from carrying stale RPC state; it does not yet
implement a pager continuation. Beast host/target and K1 gates passed, as did
DE25 K1 with matching archived ROM and log checksums. See
`docs/CURRENT_STATE.md` and
`/home/barry/astra-evidence/call-lifecycle-20261106/` on Beast. Haiku
`src/system/kernel/condition_variable.cpp:408-426` clears wait status before
unblocking; Astra keeps that ordering in its existing thread wait path.

## Existing owners

- VFS service owns open descriptions, filesystem operations, exact volume
  references, and node metadata. A backend supplies bytes; clients cannot
  name its private node token. See `sw/userspace/vfs/src/vfs_service_core.c`
  and `sw/userspace/vfs/include/astra/vfs_backend.h`.
- Kernel VM owns frames, references, page tables, COW, mappings, and the
  pressure worker. Its existing library cache shares immutable ELF pages by
  package identity, but does not represent mutable VFS nodes. See
  `sw/kernel/vm.c`, `sw/kernel/area.c`, and `sw/kernel/process.c`.
- HostFS/VFS publishes exact node and entry changes through the existing
  bounded change ring and reliable Messaging topic. Loss produces an
  invalidation; a file cache must consume that same source.
- POSIX `mmap` will adapt to the native mapping contract. It must not own a
  second cache or read files behind VFS.

Haiku references: `src/system/kernel/cache/file_cache.cpp` uses one vnode
cache for ordinary I/O and mapped pages, reads partial pages before writes,
zeroes bytes beyond EOF, and drops clean unmapped pages under pressure.
`src/system/kernel/vm/VMCache.cpp` tracks cache consumers and mapped-page
lifetime. `src/system/kernel/vm/vm_page_writer.cpp` keeps dirty writeback
separate from clean-page eviction. Astra keeps those contracts while VFS
remains a userspace service.

First implementation slice, 2026-11-06: `AstraVfsOpenFile` can retain one
cache-owned reference after its session closes. The public handle is revoked,
but the backend description remains readable through the internal retained
token; it is still counted as open for safe unmount. The final release closes
the backend node and frees its slot. Tests cover stale public handle, data
after session close, exact backend close, duplicate release, and BUSY unmount.
The Beast target build, host suite, and sanitizers passed. The same service
core test passed on DE25's AArch64 HPS; board and Beast binary SHA-256 matched
at `7b8df7034c264927ce5d5b2cd644377f3343d293c9c90823255caafbc5331428`.
Evidence is under `/home/barry/astra-evidence/stop-job-180s-de25-20261106/`
on Beast. This retained description is one necessary VFS lifetime rule; no
guest mapping or page cache consumes it yet.

Second slice, 2026-11-06: the retained description exposes the existing
backend `stat_node` operation for current size and known identity after its
client handle/session closes. The test changes the backend size after close,
checks the new size through the retained token, then checks stale-token refusal
after final release. Beast `make -C sw/userspace/vfs all test sanitize` passed.
The AArch64 core test passed on DE25 with one million forced switches; board
and Beast binary SHA-256 matched at
`7f2a8dd2409bd75cfb0caef48b04282f55a897a6fa27ef9ddd2eb2ec94299d44`.
The board trial binary was removed; all three production services stayed
active, only production loop0 remained, and 7.9 GiB was free. Evidence:
`/home/barry/astra-evidence/vfs-retained-stat-aarch64.bin` and
`vfs-retained-stat-de25.txt` on Beast. No guest pager consumes this API yet.

Third slice, 2026-11-06: retained descriptions can use the existing backend
write and sync operations after client close, with the original open rights,
write limit, short-write result, mutation generation, and backend error
propagation. The ordinary write and sync paths share the same helpers. A
retained token is rechecked after waiting for an active file, so slot reuse
cannot redirect cache I/O. Tests cover read-only refusal, short write,
write/sync failure, stale token, and one million forced switches. Beast VFS
target build, host suite, and sanitizers passed. The AArch64 core test passed
on DE25; board and Beast binary SHA-256 matched at
`67aadef4430e16096229e99ae8809736a9acdb003adb7ffbc6029c78c27584be`.
Board trial file was removed; production services stayed active, only loop0
remained, and 7.9 GiB was free. Checksummed evidence:
`/home/barry/astra-evidence/vfs-retained-write-de25-20261106/` on Beast.
This is VFS writeback access, not a guest pager or file mapping.

Fourth slice, 2026-11-06: ordinary and retained file reads now share the
same backend-read and result-validation path. A backend that reports more
bytes than requested returns I/O error with zero published bytes rather
than a clipped success, so a future page-in cannot treat an invalid count
as a complete page. Focused tests cover both public read and cache-retained
read. Beast target build, full VFS host suite, sanitizers, and one million
forced-switch operations passed. The same AArch64 core fixture passed on
DE25; board and Beast binary SHA-256 matched at
`8ed11ff50caf0f073f9fcc6350315779ca945367d6d5226f2dc331b198c3b497`.
Services remained active, only production loop0 was present, and 8.5 GiB
was free. Trial binary was removed; checksummed evidence is under
`/home/barry/astra-evidence/vfs-read-bound-de25-20261106/` on Beast.
Kernel pager and cache consumers remain open.

Fifth slice, 2026-11-06: the existing per-file wait now remembers slot
generation while it sleeps. If close and reopen recycle the slot before the
waiter runs, a matching OPEN state alone cannot make an old read use the new
node. A deterministic public READ test simulates that reuse inside the wait;
the full Beast VFS host suite and sanitizers passed. The DE25 AArch64 core
fixture passed one million forced switches with board/Beast binary SHA-256
`44eaa210fb3f425536a8978d3473aeaf51815230984235e705c319218b46bebe`.
Services stayed active, only production loop0 was present, 8.5 GiB was free,
and the board trial binary was removed. Checksummed evidence is under
`/home/barry/astra-evidence/vfs-slot-guard-de25-20261106/` on Beast.
Haiku `src/system/kernel/fs/fd.cpp:284-312` pins the descriptor object while
`get_fd()` returns it; Astra keeps its existing generation-tagged slot and
rejects a waiter whose slot incarnation changed.

Sixth slice, 2026-11-06: ordinary and retained writes share backend-count
validation. An overreported write returns I/O error and zero published bytes
instead of clipped success. The node change generation advances when a backend
reports any mutation, including an invalid count, so later cache invalidation
does not miss a potentially changed file. Public and retained tests passed the
Beast target build, host suite, and sanitizers. The DE25 AArch64 core fixture
passed one million forced switches with matching board/Beast binary SHA-256
`acc8e4f4c7628f9a2f2bf9a4555b5da4ac219eb8bda00de55385d36c7152443d`.
Services remained active with zero restarts, only production loop0 was present,
8.5 GiB was free, and the trial binary was removed. Checksummed evidence is
under `/home/barry/astra-evidence/vfs-write-bound-de25-20261106/` on Beast.
Guest pager and cache consumers remain open.

Seventh slice, 2026-11-06: `kernel_area_publish_page()` reuses reserved-area
frame allocation and shared-table publication. It copies valid bytes into a
zeroed frame before the page becomes visible; short reads leave a zero tail.
Host tests cover two mappings, absent peers on allocation failure, duplicate
and invalid publication, short and zero-byte pages. Haiku's
`src/system/kernel/cache/file_cache.cpp:345-375` clears unread tails before
cache publication; Astra keeps that ordering in its existing area owner.
Beast full kernel suite, target build and K1 pass. K1 measures a 4 KiB page at
6,783/20,000 cycles, while the ordinary user fault remains 21,952 cycles.
The same ROM passed DE25's MC68040 QEMU at 6,782/20,000 publication cycles.
The qualification harness now requires the page marker among 14 markers.
ROM SHA-256 is
`3251f12ae5e94e9dd2b66082db7c9eab70f919e42b2ff112b6961be188e53175`;
checksummed evidence is under
`/home/barry/astra-evidence/area-page-publish-20261106/` on Beast. The normal
ROM was restored and board trial files removed. This publishes a kernel-owned
page; it does not yet bind a VFS pager, track dirty pages, or expose mappings
to guest applications. Separate budgets are still required for fault handoff
and pager reply before those paths become dependencies.

Eighth slice, 2026-11-07: existing retained descriptions now accept multiple
cache owners. Each successful retain requires one release; closing the client
session revokes its handle while the backend description remains open, and
safe unmount stays BUSY until the last owner releases it. If waiting for
active I/O fails, final release keeps its reference so close can be retried. Haiku
`src/system/kernel/vm/VMCache.cpp:1616-1644` releases consumer and store
references separately and performs VFS release outside its cache lock; Astra
keeps its existing state-lock release around backend close. Beast VFS target
build, host suite, and sanitizers passed. The AArch64 core fixture passed on
DE25 with one million forced switches; checksummed evidence is under
`/home/barry/astra-evidence/vfs-retain-shared-de25-20261107/` on Beast.
Cache-object identity, guest pager, and file mappings remain open.

Ninth slice, 2026-11-07: exact node identity comparison now lives in the
existing VFS backend contract. HostFS grant-root revalidation uses it, so
mount identity, inode, and birth generation must all be present and equal;
zero mount identity cannot authorize a match. Future cache sharing will use
this same predicate. Haiku `headers/os/storage/Node.h:18-29` names nodes by
device and inode; Astra adds known birth generation because its backend can
recycle inode numbers. Beast HostFS target build and host test passed. The
same AArch64 identity fixture passed on DE25, with matching board/Beast binary
SHA-256 `371a3a0a00a9618b800ed1fcecf3097c6ad63afe8ca1bf177a0ecdf3eead130c`;
checksummed evidence is under
`/home/barry/astra-evidence/vfs-identity-de25-20261107/` on Beast.

VFS page-source slice, 2026-11-07: the retained-description owner now reads
one aligned page with repeated backend reads, refusing zero progress and
overreported counts. It checks exact volume/inode/birth identity when a
shared cache object supplied one; description-scoped objects pass no identity.
It checks file type and EOF before reading, then rechecks identity, size,
mtime, and the existing VFS node-change generation before publication.
Concurrent changes return BUSY with zero valid bytes. The service-wide
generation can retry on an unrelated write until per-node cache state exists.
Beast core tests exercise short reads, stale identity, EOF, zero progress,
overreport, truncate during read, and mutation during read. Full Beast VFS
target build, host suite, and sanitizers passed. The same AArch64 service-core
fixture passed on DE25 with one million forced switches; board and Beast
binary SHA-256 matched at
`79c6f258883067362985d3448b8ad35b79f99599216ad307adcdba4367802632`.
Log SHA-256 is
`69373fdd5dea3100ca16fce035e93461870a000151214aa1f9e6c7882deb511f`;
both are archived under
`/home/barry/astra-evidence/vfs-page-source-de25-20261107/` on Beast.
The trial binary was removed; production services remained active and board
free space stayed at 8.5 GiB. This is a reusable VFS read seam; no guest file
mapping or pager service consumes it yet.
Haiku `src/system/kernel/cache/file_cache.cpp:345-375,386-427` clears
untouched tails before publishing cached pages; Astra keeps that zero-tail
step in its kernel page publisher.

Provider-coherence slice, 2026-11-07: retained page reads now sample the
backend's optional node-change counter before and after filling a page.
HostFS implements this through its existing provider change snapshot; the
same counter feeds filesystem notifications. The read also compares known
mtime nanoseconds, ctime seconds/nanoseconds, and timestamp validity in
addition to size and mtime seconds. Any change returns BUSY with zero valid
bytes; a failed provider snapshot propagates its error. Host tests cover
counter changes, timestamp changes, validity loss, and snapshot failure;
Beast target build, host suite, and sanitizers passed. The AArch64 service
core fixture passed on DE25 with one million forced switches. Board and Beast
binary SHA-256 matched at
`e216833cd37dd55165f71c91e07d695d2ce70e6df6b833abdff96e318e5b9133`;
log SHA-256 is
`69373fdd5dea3100ca16fce035e93461870a000151214aa1f9e6c7882deb511f`.
Evidence is under
`/home/barry/astra-evidence/vfs-page-coherence-de25-20261107/` on Beast.
HostFS notification delivery can lag a direct external write, so the future
cache must also consume node and gap notifications before reusing pages.
This slice does not create a file mapping or pager service.

Pager reclaim slice, 2026-11-07: Existing address-based `AREA_DECOMMIT`
refuses pager-bound areas, so a file cache could not return clean resident
frames under pressure. Native `AREA_RECLAIM` now requires an ADMINISTER area
handle, aligned offset and length, and no in-flight child fault. It calls
the existing page withdrawal path, which clears every shared mapping before
releasing the frame. A later access faults through the same pager. Kernel
tests cover absent pages, alignment, in-flight refusal, withdrawal, and
refault; syscall and Runtime tests cover wrong handle and rights. K1 samples
the operation against a 125,000-cycle budget. Beast and DE25 K1 passed all
15 markers with zero overruns; both measured 200 cycles. Matching ROM SHA-256
is `57778de14b12ac03c47034103feae8018444d8e9b6c9591f7c5ecbc5b81262a9`;
DE25 log SHA-256 is
`2860eebcfe1f93cc93eae34d596a21f7f7037c8ac093620f544b136492608dc3`.
Evidence is under `/home/barry/astra-evidence/area-reclaim-k1-20261107/`
on Beast. Haiku `src/system/kernel/cache/file_cache.cpp:306-341` checks
clean and unmapped state before eviction; Astra's pager areas cannot have
writable mappings, and VFS must write back dirty content before reclaim.
VFS cache ownership, notification-driven invalidation, and reclaim policy
remain open.

Refault gate, 2026-11-07: the area host test now proves one reclaimed page is
withdrawn from two address spaces and both can request it again. The K1 guest
pager serves a second request after reclaim, checks original bytes and zero
tail, then retires its service thread. Beast and DE25 K1 passed all 15
markers with two pager replies and no overruns; DE25 reclaim cost
200/125,000 MC68040 cycles. ROM SHA-256
`a0dd6433326aabe6ee4ea2ad765c78761690e18a961a75f3623e3b57989be2d2`;
DE25 log SHA-256
`092d89f3804f614119137d563a669a6565e55e9803be3f570647049cc1d5d5f6`.
Checked evidence is under
`/home/barry/astra-evidence/pager-refault-k1-20261107/` on Beast.
This proves kernel withdrawal and refault, not VFS cache invalidation.

Pager candidate, 2026-11-07: a bound-area fault now sends the page request
through the existing port call/reply machinery, keeps the area pinned while
waiting, reads the reply's transfer area, and publishes only a checked,
zero-tailed page. Wait completion restores the faulting registers and releases
the pin. Invalid replies produce the ordinary inspectable user-fault verdict.
Focused host tests cover success, invalid reply, a full send queue, and two
threads faulting the same page with either a full or available queue; the
latter queues one request and wakes both after publication.
Beast kernel tests, target build, `verify`, and 14-marker K1 gate
pass. Haiku `src/system/kernel/vm/VMCache.cpp:950-966,1563-1577` tracks page
waiters and wakes them after publication; Astra currently reuses its native
thread wait observer and port reply capability. A full-port wake now retries
the unsent request in the kernel before the user instruction can run. This
candidate was not a release: terminal lifecycle gates, VFS pager service,
and DE25 guest qualification remained open at that checkpoint.

Guest pager gate, 2026-11-07: K1 survivor creates a reserved area, binds
its port and cookie, and faults on its read-only mapping. A second MC68040
thread checks the request and answers with five bytes from a separate
committed area. The faulting instruction resumes and verifies source bytes
and zero-filled tail, then joins service and retires mappings and handles.
This follows Haiku `src/system/kernel/vm/VMCache.cpp:950-966,1563-1577`
page-wait ordering, using Astra's native port and area ownership. Beast
and DE25 K1 gates passed all 15 markers; DE25 ROM SHA-256 is
`9c6d52c1767a19ee8299fdd061247c41b18c5c00f6d8806834210416cfbe7c2a`.
Checksummed log and ROM are under
`/home/barry/astra-evidence/pager-guest-k1-de25-20261107/` on Beast.
This gate uses a synthetic service; VFS page replies, general cached-file
mapping lifecycle, dirty writeback, reclaim, and pager cycle budgets remain
open.

Pager CPU budget gate, 2026-11-07: existing `kernel_performance` counters now
measure fault request, pager reply/publication, and wait completion/handoff
separately. Each is required by the K1 release mask, and the guest pager
test supplies its sample. Beast and DE25 measured 1,291/50,000,
2,981/75,000, and 79/25,000 MC68040 cycles. Earlier DE25 baseline at
`6b715ab4` measured user-fault maximum 21,994 and standalone page
publication 6,781 cycles; the instrumented gate measured 22,022 and 6,782.
ROM and log SHA-256 are
`ccab46d461c5fbe26925aca22a07c0a77df6068d38afc4783d4e96596814bf4c`
and `0bbad1d850b9320ef6c49ed4776ff5382ce51a1d8674e46d3091d3dd39b8f15b`.
Evidence: `/home/barry/astra-evidence/pager-cycle-budget-de25-20261107/`
on Beast. CPU cycle budgets do not qualify asynchronous VFS, storage, or
end-to-end page-fault latency; those await real file mapping.

## Target baseline, 2026-11-06

Three focused `fsbench` runs on DE25 used the selected release's kernel and
VFS with `fsbench` added only to a disposable system image. Each run prepared
200 small files and two 4 MiB files under a new `/work/fsbench-cachebaseN`
directory. No file cache or mapping change was present. Raw guest results and
matching board/Beast SHA-256 checksums are archived under
`/home/barry/astra-evidence/file-cache-baseline-20261106/` on Beast.

| Existing path | Median guest time, three runs | p95 guest time, three runs |
|---|---:|---:|
| `stat` warm, 200 operations | 5.059-5.193 ms | 7.053-9.257 ms |
| open/close warm, 200 operations | 4.676-4.760 ms | 6.804-7.864 ms |
| sequential read, 64 × 64 KiB | 2.716-2.732 ms | 3.198-3.668 ms |
| random read, 1024 × 4 KiB | 2.219-2.280 ms | 3.265-3.608 ms |

Sequential throughput was 22.95-23.99 MB/s; random throughput was
1.60-1.69 MB/s. `fsbench` reports `ns / 80` as *guest wall-time-equivalent*
MC68040 cycles, not cycles spent on CPU: the 4 KiB read medians were
27,736-28,500 equivalents and include waits for VFS and HostFS. The existing
Beast K1 qualification measured user-fault dispatch at 21,930 CPU cycles
against its 125,000-cycle budget, with zero overruns
(`mutex-k1-baseline-20261105.log`). These readings define the unchanged-path
regression comparison; they do not measure a future pager or file-cache hit.
Target-representative area-map and pressure-worker dispatch measurements are
recorded below. Actual pressure reclaim measurement and separate budgets for
new fault handoff and pager completion remain required before those paths
become dependencies.

The existing K1 qualification harness already records area-map and pressure
worker metrics. Its output now retains those lines and raw sample counts.
At source commit `6301cabc`, Beast MC68040 QEMU with `-icount shift=4` passed
all 13 markers: user fault 21,950/125,000 cycles (one sample), area map
1,415/125,000 cycles maximum (two samples), and area unmap 562/100,000 cycles
(one sample). No pressure reclaim ran, so this gate supplies no pressure-worker
latency baseline. Log:
`/home/barry/astra-evidence/file-cache-k1-baseline-20261106.log` on Beast;
qualification ROM SHA-256:
`eb2081ce8fea6401601b8ad781f620fe618408f022d0cb391f797970764e92b3`.
The same qualification ROM and harness ran on DE25's AArch64 HPS against a
disposable copied image. All 13 markers passed in 55.6 seconds, with user fault
21,950 cycles and area-map maximum 1,415 cycles. The board and archived Beast
log have matching SHA-256
`47309d311c12bf2e667998128499209012c47195b05cd9c78708b472fea8ac35`;
evidence is under `/home/barry/astra-evidence/file-cache-k1-de25-20261106/`.
Astra, audio host, and remote desktop were restored; only production loop0
remained, staged board files were removed, and 7.9 GiB was free. These are
instruction-timed MC68040 samples on the real host, not wall-clock HPS
latencies. Two area-map samples cannot establish tail latency.

The K1-only workload now signals the existing pressure worker once after its
registration. At normal memory pressure the callback returns without reclaim;
this isolates dispatch overhead. Beast and DE25 each recorded one 22-cycle
`KERNEL_PERFORMANCE_LIBRARY_CACHE` sample, with all 13 markers passing. The
DE25 log SHA-256 matched its Beast archive at
`/home/barry/astra-evidence/file-cache-pressure-worker-k1-de25-20261106/`:
`1850d0604c303c6ba06f2c15454d144241e10211e0f82a1f6f6b0d24e18a8761`.
Qualification ROM SHA-256:
`60595e574290ca9afc772cee0a8392e65f0b0c4797360551cb4972c12f74807b`.
Services and selected release were restored, only production loop0 remained,
trial files were removed, and 7.9 GiB was free. Actual reclaim cost under
pressure and tail latency remain unmeasured.

## Native contract

1. A cache object names one open VFS node on one exact volume incarnation.
   Share it across opens only when the backend reports volume, inode, and
   birth generation. Otherwise keep an open-description-scoped object;
   unknown identity must never merge two files. The VFS owner holds the
   backend description until the last cache/mapping reference retires.
2. Kernel VM owns cached frames and mapping references. VFS owns page I/O
   and file size. Faults request pages from the VFS owner through the existing
   native call/reply and wait-owner donation path. A fault must suspend its
   thread before that call can block; the current synchronous exception path
   has no pending pager-reply state or blocking userspace handoff.
   Pager service memory needed to answer a fault must be resident or otherwise
   avoid faulting on the same cache object.

   The existing `ASTRA_SYSCALL_PORT_CALL` path in `sw/kernel/process.c`
   starts a `kernel_thread_call_*` wait, transfers a reply capability, and
   donates priority to the receiver. Its `deliver_reply()` writes the answer
   into the caller's user buffer. A pager call must reuse that wait, reply
   capability, and donation lifecycle while retaining its answer in kernel
   state; it cannot put a fabricated user address in `call_reply`. The fault
   dispatcher currently requires an immediately runnable context and the
   fault handler retires every unresolved access. Add a blocked-fault
   scheduler handoff and a pending-fault continuation before sending any
   pager request. Reply, timeout, cancellation, pager death, and process exit
   must each retire that continuation exactly once.

   A page is 4 KiB, but `ASTRA_MESSAGE_INLINE_MAX` is 1 KiB. Page bytes must
   travel through one existing native shared area, as VFS bulk read/write
   already does with `BIND_AREA` and `READ_AREA`/`WRITE_AREA`. The call/reply
   carries only identity, offset, length, status, and transfer ownership.
   Bound area and reply capability must remain valid for the entire pending
   fault; timeout or death revokes both before frame reuse. No partial read
   becomes a mapped page: zero the page tail and publish only after the full
   result and current volume incarnation are checked.

   `AstraVfsOpenFile` in `sw/userspace/vfs/src/vfs_service_core.c` starts with
   one session. Closing that session revokes its public handle; retained cache
   references keep the backend description open until their final release.
   A mapping therefore cannot hold only the session token. Its identity
   comes from `AstraVfsNodeMeta.valid`; only the complete volume, inode, and
   birth-generation triple permits cross-open sharing.

3. Ordinary reads and writes and mapped access use the same cached pages.
   Partial writes preserve untouched bytes; short reads zero the remainder of
   the final page. `MAP_PRIVATE` uses existing COW. `MAP_SHARED` writes mark
   pages dirty and `msync`/unmap/close use VFS writeback and sync, reporting
   errors rather than dropping dirty data. Clean read-only pager pages may be
   withdrawn from mapped addresses and refaulted; busy or dirty pages remain
   resident.
4. Truncate revokes pages beyond new EOF before access can resume. Page faults
   beyond EOF fail without exposing stale frame contents. Unmount reports
   mapped/open users through the existing volume-usage path and refuses safe
   removal until they release the node. A stale volume incarnation never
   services a pending page request.
5. Exact node changes invalidate clean pages and revalidate size. A GAP
   invalidates all affected cached identities. If an external writer changes
   a node with locally dirty pages, preserve those pages and report a write
   conflict to the owner; never silently merge or discard either version.
   Notifications are an invalidation signal, not a substitute for VFS's own
   ordered writes.
6. The existing pressure worker reclaims clean file pages after idle libraries,
   before protected reserves. Read-only pager pages can remain mapped because
   shared translations are withdrawn before frame release. Dirty writeback is a separate
   bounded worker operation; memory pressure must not synchronously perform
   filesystem I/O inside the allocator or fault handler. Failure leaves
   dirty pages resident and visible in accounting.

## Pager wiring through existing native objects

### Shared page owner required for ordinary I/O

`HostfsFileArea` now keys exact nodes by volume, identity, and fixed 128 MiB
chunk base. Incomplete identities still get separate owners. Ordinary client
reads already use `astra_vfs_port_read_cached_position()` and those canonical
pages; do not add a second read-cache hook. The service's backing readers stay
cache-bypassing. Ordinary `write_file()` still reaches raw backing I/O, followed
by HostFS page invalidation. `kernel_area_map_range_at()` maps aligned subranges
and POSIX joins adjacent chunks. Keep the remaining write work on that same
owner path, without a second userspace byte cache or application-specific path.

Checkpoint52 extends the existing I/O cell to exact nodes across independent
opens. Use that single owner for node-wide mutation and logical EOF across
all chunks; do not introduce separate node and description locks. Existing
backend-binding callbacks run under the service state lock and prohibit
I/O/reentry; they cannot become blocking cache callbacks.
Cache preparation must use backing page reads directly rather than wait for
its pager while holding the description active. Zero the unread EOF tail
before publishing a complete dirty page. Backing writeback must bypass the
current mutation-invalidating wrapper, which would otherwise reject its own
dirty pages. Public stat/FILE_AREA must observe logical cached EOF, while raw
backing stat remains the writeback validation source. Failed flush/close must
retain dirty frames and their native writer authority.

Extend the existing `KernelArea` mapping record with source page offset and
mapped length, and give the native area API an offset/length map operation.
The VFS owner can then retain one cache object's area for a node incarnation
and hand clients views onto its pages. Bound the owner by the existing area
address-space limit; adjacent owners must still support one contiguous client
mapping when a file crosses that limit. Ordinary VFS reads and writes must
address the same owner pages. Their backend I/O and metadata checks remain in
VFS; kernel VM remains the sole frame and mapping owner. Complete identity
allows cross-open sharing; incomplete identity keeps the retained-description
scope. The current change topic remains the only external invalidation feed.

For writable shared views, the existing shared-table write protection can
trap the first write, but `kernel_vm_shared_table_set()` only publishes an
absent entry. Add a checked descriptor transition and per-page dirty state
before enabling writes. `MAP_PRIVATE` needs per-process COW translations;
globally changing the shared table would leak one client's private write to
other mappings. `msync`, unmap, and last close must report failed writeback
and retain dirty frames. New fault and write-protect paths require K1 cycle
budgets before use by higher layers. Acceptance tests must cover overlapping
range identity, ordinary read/write coherence, partial-page preservation,
private isolation, shared dirty writeback, EOF/truncate, external conflict,
pressure, and exact mapped-user safe-removal attribution.

Haiku `src/system/kernel/cache/file_cache.cpp:760-850` locks one vnode cache
while ordinary I/O uses its pages; `src/system/kernel/vm/VMCache.cpp` owns
those pages and `src/system/kernel/vm/vm_page_writer.cpp` handles dirty-page
writeback. Astra adapts that ownership to its native area, VFS service, and
existing pressure worker.

Moving ordinary I/O behind this owner must preserve the grant's rooted
backend, not substitute the volume service's whole-volume backend. The current
grant port deliberately refuses data operations for that reason. Session/file
context retains the scoped backend through client close and cache lifetime.
The scoped backend prerequisite is qualified on DE25 (2026-11-11): session
admission acquires a rooted binding, every open file retains its own binding,
and recovery replaces only the session binding. Existing open/retained files
keep their original provider incarnation; mixed-incarnation anchored operations
fail before backend I/O. HostFS uses its existing backing-reference accounting
to retain grants through both contexts and areas. This does not enable the
grant data plane or establish requester attribution.
Provider share tokens remain bound to their provider session; forwarding a
volume-wide service token is not equivalent authority. Provider-side opens
and retained cache references must still attribute actual native requesters
for quotas, process cleanup, `lsof` and safe removal. The service's own PID
cannot stand in for every application. These are migration requirements,
not authorization to expose the existing raw data handlers on grant ports.

The VFS service creates one reserved area per cache object and binds its own
port's send endpoint and a nonzero, service-owned cache cookie to that area.
VFS must generation-tag the cookie and avoid reuse while an old area or
request can still name it. A single port can therefore serve several files
without treating an offset as a global identity.
`kernel_port_handle_retain()` keeps the endpoint alive until the area retires;
receiver death makes sends fail with
`PEER_DEAD`. VFS transfers only the area handle to mapping clients. The
kernel checks an existing area mapping on a fault, so a client cannot fault
an ungranted file into memory. Unknown backend identity gets a separate area
per retained description; complete volume/inode/birth identity permits area
sharing. The VFS description stays retained while the area or a pending page
request exists.

For a missing page, the kernel installs its existing reply capability in a
kernel-owned transient handle table and sends a request to the bound port with
`kernel_port_send_narrowed()`. The faulting thread then uses
`kernel_thread_call_begin/block/assign()` and `schedule_next(SWITCH_BLOCK)`,
the same donation and deadline machinery as `ASTRA_SYSCALL_PORT_CALL`. A full
port queue must wait for writable capacity and retry the unsent request; it
cannot resume the faulting instruction with the page still absent. The
exception dispatcher must accept a blocked fault with no runnable user thread
and select the existing worker/idle path, as syscall dispatch already does.

The VFS pager reads through its retained description into a separate,
already committed transfer area. Its reply carries that area handle, offset,
valid byte count, and status. `deliver_reply()` recognizes a pending fault
before its ordinary user-buffer branch, looks up the transfer area in the
replier's handle table, copies a checked page into a zeroed kernel-owned frame,
and publishes the frame only after the complete result and current area,
volume, and file generation pass validation. No reply handle is imported into
the client and no user address is fabricated. A second thread faulting the
same page waits on that page's pending operation rather than issuing a second
read. Successful completion wakes all waiters only after publication.

Reply-capability release, deadline expiry, cancellation, service death, and
process exit each retire the pending fault once. Failure leaves no published
page and terminates the faulting access with an inspectable crash result;
retrying the same unresolved instruction indefinitely is invalid. The
transfer area is never the file's mapped area: writing into a published shared
page while it is being filled would expose partial data to other processes.

Fault-continuation control-flow audit, 2026-11-07: `sw/kernel/process.c`
already retains a call number, reply capability, receiver donation, deadline,
and a wait observer, but its ordinary reply copies into a user address. A
pager continuation must instead keep the pinned area and aligned offset on
the faulting thread, accept a service-owned transfer-area handle in its reply,
read the page through the existing area API, and publish it before completing
the call. The reply handle is consumed; the transfer-area handle stays in the
service and is never imported into the client. The wait observer releases the
area pin exactly once on reply, timeout, cancellation, or process exit.

The observer runs inside `sw/kernel/thread.c:complete_wait()`, before the
thread is enqueued. It must not retire the process reentrantly. A failed
continuation instead needs one common return-to-user gate in
`sw/kernel/process.c:activate()`: retire each selected failed-fault process with the
original fault snapshot, select another runnable thread or existing idle/
worker path, and only then return a user context. This avoids fabricated user
addresses, invalid-PC traps, silent zero pages, and unbounded recursive
scheduling. A full pager port blocks on its existing writable wait queue;
`activate()` retries the unsent page request before returning to user mode,
using the original deadline. The waiting thread remains the page's primary
operation so later faults join it, including while the send queue is full.
One pending operation owns each `(area, page)`; later
faults wait for its publication or terminal failure through existing thread
wait queues. Haiku `src/system/kernel/vm/vm.cpp:3705-3780` keeps fault
authority while resolving a page, and `src/system/kernel/cache/file_cache.cpp`
publishes only complete, zero-tailed data. Astra adapts those contracts to
its current area, port, and scheduler owners.

Host tests now cover success, malformed reply, stale replies after successful
publication and timeout, full send queue, original deadline retention, timeout,
receive-end closure, and two simultaneous faults on one page with both full
and available send queues. Process exit with pending same-page faults retires
their area references. Pager donation and restoration are asserted.
Area child references return to zero on tested success and failure paths.
K1 now enforces separate request, reply/publication, and terminal-handoff
CPU cycle budgets against the earlier user-fault baseline. End-to-end VFS
page-fault latency remains unmeasured.

This wiring uses existing `sw/kernel/area.c` mapping authority and frame
ownership, `sw/kernel/port.c` transfer, `sw/kernel/thread.c` call donation,
and `sw/kernel/process.c` reply capability. Haiku's
`src/system/kernel/vm/vm.cpp` resolves faults against area/cache ownership,
while `src/system/kernel/cache/file_cache.cpp` keeps data I/O behind the vnode
cache; Astra adapts those rules to its userspace VFS service. No new IPC
implementation or Linux-visible file-mapping API is needed.

The first scheduler prerequisite now passes: fault dispatch accepts
`NO_RUNNABLE` with no maintenance and chooses the existing worker/idle path.
Host tests cover all three outcomes. Beast and DE25 K1 qualification passed on
the same ROM (SHA-256
`2e229974c0d891e484887ce49673f52dfa078a90c684f91e2d9e2af0ab16c0d9`)
with zero cycle overruns; Beast's normal user-fault sample remained
21,950/125,000 cycles. Evidence:
`/home/barry/astra-evidence/pager-fault-idle-20261106/` on Beast. No pager
request can block yet; pending-fault state, reply delivery, cache pages, and
mapping API remain open.

The next scheduler prerequisite now passes: one observer can attach to an
already blocked thread and is cleared before invocation on wake, RPC reply,
deadline expiry, cancellation, or process retirement. `complete_wait()` and
process retirement are the only wait-withdrawal exits. Tests exercise each
exit and duplicate registration. Haiku's
`src/system/kernel/thread.cpp:3035-3168` retains a blocked kernel stack and
`src/system/kernel/condition_variable.cpp:165-212` withdraws its condition
entry when that wait returns; Astra cannot retain the exception stack, so this
bounded observer supplies the completion edge for an explicit continuation.
Beast K1 passed 13 markers with zero overruns and 21,951/125,000 ordinary
user-fault cycles; the same ROM booted DE25 to `KERNEL MULTITASKING` with no
storage image, panic, or overrun. Evidence:
`/home/barry/astra-evidence/wait-observer-k1-20261106/` on Beast. No pager
operation consumes this observer yet.

The area ownership prerequisite now passes: an administering service binds a
reserved area to its port send endpoint before any mapping or committed page
exists. The area retains that endpoint and releases it on close. Bound areas
permit read-only client mappings but refuse anonymous fault zero-fill,
unpublished kernel reads, writes, and client decommit. K1 uses a real port and
the bound area for its existing atomic page-publication gate; Beast and DE25
both passed with 6,782 publication cycles against the 20,000-cycle budget.
Evidence: `/home/barry/astra-evidence/pager-bind-20261106/` on Beast. This is
an ownership and safety gate, not a working pager: fault request, reply
continuation, cancellation, VFS page lifecycle, and mapping API remain open.

The next area seam carries a nonzero VFS-owned cookie and returns an authorized
missing page's area, port, cookie, and aligned offset together. Lookup takes
one area child reference so a pending request can hold area and endpoint
through its reply or cancellation. Host tests cover two distinct cookies on
one port, an ungranted process, and a page already published. K1 checks the
same identity on MC68040. The future request must consume this pin exactly
once; the lookup alone sends no message and resumes no fault.

## Implementation order and release gates

1. Measure current fault, area-map, VFS read, and pressure-worker cycles on
   DE25; set budgets for the new fault and pager path before it becomes a
   dependency. Reuse existing native RPC donation, frame references, COW,
   and volume identity rather than adding a second scheduler or file layer.
2. Add fault suspension/reply/cancellation with tests for pager death,
   timeout, concurrent faults on one page, exit during fault, and priority
   donation. A failed page-in must leave no half-published mapping.
3. Add one VFS-backed cache object and page lifecycle. Test identity reuse,
   sparse/short reads, EOF zeroing, partial writes, truncate, sync failures,
   and shared/private mapping coherence against multiple processes.
4. Wire the existing change topic to clean-page invalidation and GAP recovery.
   Test canonical writes, Linux-originated writes, rename/unlink, and stale
   volume refusal. Verify exact unmount-busy attribution for a live mapping.
5. Join the existing pressure worker; prove clean-page reclaim and dirty-page
   retention under allocation failure. Adapt POSIX `mmap`, `munmap`, and
   `msync` onto this native cache only after native gates pass.
6. Pass host kernel/VFS tests, QEMU native and POSIX gates, then a coherent
   DE25 release trial with concurrent readers/writers, external invalidation,
   pressure, and safe removal. Compare cycle budgets with the baseline and
   inspect generated MC68040 code before any assembly optimization.

Current remaining integration gates (2026-11-14):

Native extent prerequisite71 reviews Haiku `cache/file_cache.cpp:1387-1401`
and `vm/VMCache.cpp:1188-1228`: cache length is independent of view capacity;
truncate discards pages outside EOF and zeroes the partial tail. Astra adds
one backing-length word to the existing area, preserving the one-frame leaf
allocation for eight records (328 bytes each on MC68040). The ADMINISTER-only
resize operation reuses pager epochs, VM unmap/COW ownership, byte clearing and
dirty pins. It revokes copied private pages even after source reclaim, refuses
affected in-flight snapshots before mutation and permits owner staging before
growth. Source capacity and view reservations do not change. Client access uses
the published extent; owner copy-in uses physical residency, avoiding an endless
attempt to republish an existing invisible page. Host tests cover that distinction,
forked views, zero EOF, short tails, staging, rights and stale replies. The first
K1 full-window probe kept a second view in the128MiB window and failed admission;
the corrected probe releases the old view before reserving the full window.
Its shrink costs470770/2000000 cycles, all28 markers pass, zero overruns, and
private read/write faults remain537/441 against25000. Kernel/Runtime host and
sanitizer gates, normal ROM and NDK distribution pass. Final staging correction
passes those kernel/Runtime gates, then all39 native cases, three extra pressure/
scoped/owner rounds,48 publication races, both recovery checks, private mappings,
shutdown88 and fsck in144.9s. DE25 passes the new native resize probe and all eight
packed pressure/session cases, private mapping/fork/reclaim, final publication/
root checks and shutdown88 at400/600 guard iterations. All four observers return
zero. Archive `be1f1d54bd20f8febd38c0b86954d7905195ee83446176fbd34e46fcbc5dde2c`
verifies572 hashes, exact manifest, paired settings/key, unchanged boot/selector
and read-only fsck before disposable board state is removed. Selected services
restore at fresh stage8, zero restarts, only loop0 and8.6GiB free. Evidence is
retained under `cache-extent-20261114`; that checkpoint alone is not HostFS EOF
or POSIX mapping completion. Checkpoint72 above joins initial owner length,
accepted growth, truncate and external refresh to native node I/O, including
idle external changes and dirty-conflict ordering. Its complete native and
DE25 gates pass; POSIX guards remain until their own native-backed contracts
are tested. There is no second file cache or POSIX-owned backing length.

| Gate | Current boundary and required proof |
|---|---|
| Mapping publication fence | Checkpoint65 moves production FILE_AREA admission, metadata refresh and publication under existing native node I/O. Its independently retained source preserves the original backend through close/session death. Reclaim skips active nodes; stale exact owners are revalidated instead of replaced. Prior reclaim policy fails the regression; the focused stale-owner reuse regression is fixed. Host/sanitizer/NDK and all 39 native cases pass. DE25 passes nine cases including cache pressure, both recoveries, repeated publication/root checks and shutdown88. Verified archive/cleanup preserves selected data. Checkpoint67 qualifies ordinary cached reads/writes and the bounded worker on DE25. Complete mapping semantics and exact mapped-user attribution remain open. |
| Shared-description serialization | Checkpoint49 replaces per-handle I/O ownership with one shared description cell inherited through SHARE/ADOPT, using the existing file budget and state mutex/futex. Cell lifetime is independent of handle reuse; alias close does not release another handle's operation. Page-fill fencing uses the shared sequence. Two-anchor mutations deduplicate aliases, acquire distinct owners in stable order and revalidate after waits. Host/ASan/UBSan/TSan and all 37 native cases plus provider reset, HostFS replacement, clean shutdown 88 and read-only fsck pass. DE25 passes eight initial cases, provider reset, HostFS replacement/fresh I/O, post-replacement map-publication and clean shutdown 88 in 274/600 seconds; all four observers return zero. The sealed archive verifies all 59 hashes, paired settings/key, unchanged Linux boot identity/selector and read-only fsck. Private board state/release/spool are removed; selected services are healthy at stage 8 with 8.6 GiB free. An optional second scoped-I/O probe was not installed because harness transfer failed; no second hardware scoped-I/O pass is claimed. This supplies the cursor/commit serialization required by cached writes without widening client read access. |
| Single-owner publication | Checkpoint48 fixes a host-forced construction race: after allocation/pager binding, selection is repeated and handle duplication/publication are atomic under the existing state lock. Losing candidates release their unused area and retained reference. Native independent-client races prove shared resident pages across eight cold files; host tests also preserve grant, chunk, volume and incomplete-identity boundaries. DE25 passes eight cases, provider reset, HostFS replacement, a second concurrent-publication run and clean shutdown 88 in 281/600 seconds; all four observers return zero. The sealed archive verifies all 59 hashes, paired settings/key, unchanged Linux boot identity/selector and read-only fsck. Private board state/release/spool are removed; selected services are healthy at stage 8 with 8.6 GiB free. Haiku `fs/vfs.cpp:4796-4866` supplies the rechecked publication pattern. |
| Scoped mutation ownership | Ordinary I/O and FILE_AREA use authenticated native sessions/descriptions (wire 42, Filesystem Kit ABI 18); raw direct adapter and FILE_MUTATED are deleted. Host suites, all 36 DE25 native cases, optical/UI, provider reset and HostFS-death recovery pass. Trials38–40 total five death/recovery cycles and three clean shutdowns. Diagnostics41 preserve shutdown stage/service/PID/native status/reason without allocating. DE25 trial43 additionally proves exact controlled transport failure, fresh-Terminal recovery, faulty-service removal, HostFS-death recovery and clean shutdown: all four results zero in 326/600 seconds. Trial31's earlier shutdown failure remains unexplained; repeated passes do not establish its cause. Archive/verify and board cleanup complete each retained checkpoint. |
| Cache writeback | Checkpoint50 adds owner-authorized atomic publication of a complete absent page as dirty, reusing existing page and dirty/snapshot lifetimes. Native and DE25 K1 pass all 24 markers; publication peaks at 7,201/25,000 cycles against the 7,128-cycle publish-plus-mark baseline. Checkpoint51 qualifies that runtime wrapper in the normal-service page-writeback case and adds WRITE-authorized backing reads for partial-page preparation across HostFS, RAM and ext4. Host/sanitizer, native and DE25 gates pass. Haiku `cache/file_cache.cpp:515-713` guides prepare-before-publish. Checkpoint67 connects production read/write/sync and pressure/shutdown work to these owners. Namespace/metadata ordering, accepted prefixes, dirty retention, pressure followed by rooted grants, all39 native cases and a coherent DE25 trial pass. Logical timestamp persistence and saved-error precedence still require proof/fixes; mapped modifications remain open. Qualification uses the production path as well as helper probes. |
| Shutdown recovery | Checkpoint47 qualifies HostFS failed-retirement ownership and retry, preserved native endpoints/sessions, dependency-ordered restoration and the planned-stop/dead-process race on DE25. The complete gate passes in 365/600 seconds, including fresh Terminal/I/O, provider reset, HostFS replacement and clean shutdown 88; the sealed archive passes read-only fsck. Original trial31 refusal is still unexplained and Desktop consent/status UI remains open. This does not qualify cached mutation or writable mappings. |
| Mapping completeness | Checkpoint73 qualifies POSIX writable private file mappings through native COW, including read-only descriptors, close/fork isolation and cross-owner copies. Checkpoint74 qualifies native mapping-owned lifetime, dead-pager retirement and POSIX anonymous private/shared maps on host/target/DE25. Checkpoint75 qualifies native page-range trim/split and POSIX partial unmap, deletes the POSIX mapping registry and proves surviving aliases across fork, reclaim, snapshot protection and EOF on DE25. Checkpoint76 qualifies native shared WRITE authority within description/source/grant ceilings, retained-writer lifetime after description/handle close, coherent ordinary I/O and exact cross-window backing bytes on DE25. POSIX shared file maps remain read-only pending native address-range sync and msync; complete POSIX fault/EOF semantics remain open. |
| EOF and external conflicts | Checkpoint72 passes native owned truncate/growth across fork and external Linux changes with the guest target descriptor closed: private partial-tail zeroing, child-only full-page EOF faults and fresh growth. Host RED proves a later dirty window refuses external refresh before any private extent changes. Existing NODE/GAP observation uses retained native stat under node I/O. POSIX fault semantics and mapped shared-write conflicts still require their integration gates. |
| Mapped-user attribution | FILE_AREA authenticates requester/session/grant and retains the native file. Native session/file holder data contributes to usage inventory; exact kernel mapping attribution after file close and fork remains unfinished. Verify safe-unmount/lsof/UI explanations; a reference count or HostFS-owned descriptor alone cannot prove who is using a mapping. |
| Final release | Run concurrent native/POSIX I/O, provider loss, external changes, pressure, durability and mapped-user removal on one coherent DE25 candidate. Complete the still-unperformed literal USB cable replug gate when the user is available; preserve production data and clean trial storage. |
