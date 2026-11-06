# General file cache and file-backed mappings

Status: design contract; implementation and certification open.

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
3. Ordinary reads and writes and mapped access use the same cached pages.
   Partial writes preserve untouched bytes; short reads zero the remainder of
   the final page. `MAP_PRIVATE` uses existing COW. `MAP_SHARED` writes mark
   pages dirty and `msync`/unmap/close use VFS writeback and sync, reporting
   errors rather than dropping dirty data. No page is evicted while mapped,
   busy, or dirty.
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
6. The existing pressure worker reclaims clean, unmapped file pages after
   idle libraries, before protected reserves. Dirty writeback is a separate
   bounded worker operation; memory pressure must not synchronously perform
   filesystem I/O inside the allocator or fault handler. Failure leaves
   dirty pages resident and visible in accounting.

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
