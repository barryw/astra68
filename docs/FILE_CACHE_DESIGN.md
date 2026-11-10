# General file cache and file-backed mappings

Status: design contract; VFS description-retention, metadata, VM page
publication, and read-only HostFS file areas passed host and DE25 gates.
The existing pressure worker reclaims clean read-only pager frames on DE25.
Sequential/borrowed reads and ordinary writes still bypass mapped resident frames; dirty writeback,
full EOF/truncate mapping semantics, and writable/anonymous POSIX `mmap`
remain open.

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
chunk base. Incomplete identities still get separate owners. `read_acquired_file()` and `write_file()` in
`sw/userspace/vfs/src/vfs_service_core.c` call backend I/O directly, so
ordinary I/O does not consume those frames. `kernel_area_map_range_at()` maps
aligned subranges and POSIX joins adjacent chunks. Keep remaining VFS work as
one owner path, not a second
userspace byte cache or an Atlas/SDL-specific path.

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
