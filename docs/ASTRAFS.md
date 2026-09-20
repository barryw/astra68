# AstraFS design

Status: deferred architecture. No AstraFS implementation, on-disk format, VFS
extension, command, or compatibility promise exists yet. ext4 remains Astra's
qualified writable filesystem. This document records the design and acceptance
bar for future work so implementation does not begin from an informal sketch.

## Purpose

AstraFS is a future native filesystem for Astra. Its distinguishing features
are typed file attributes, volume-wide attribute indices, efficient structured
queries, and eventually gap-free live query notifications. It is inspired by
the useful desktop-oriented ideas in the Be File System, but it is a new Astra
format and does not preserve BFS disk compatibility, implementation structure,
or historical limitations.

AstraFS is not required to boot the system and does not replace ext4 while it
is being developed. It will first run as an additional filesystem backend on
disposable images. It becomes eligible for system data only after it meets the
recovery, corruption, performance, and physical-hardware gates in this
document.

The design has four priorities, in order:

1. committed data and namespace operations survive interruption;
2. corruption is detected, contained, and reported rather than guessed around;
3. metadata search is a native filesystem operation rather than an application
   database kept loosely synchronized with files; and
4. the implementation remains fast and compact on one 70 MHz MC68040.

## Non-goals for the first format

The first production-capable format does not include snapshots, deduplication,
transparent compression, filesystem-level encryption, writable clones, or
online defragmentation. None is required for a journaled, resilient, modern
filesystem, and each multiplies the state that crash recovery must prove.

AstraFS does not add filesystem interpretation to Axiom. The kernel continues
to provide process isolation, handles, areas, IPC, and the block-device lease.
The storage service owns the filesystem implementation in userspace.

AstraFS does not expose raw disk blocks to network clients. A future network
filesystem transports versioned VFS operations and may export either ext4 or
AstraFS from the server.

## Relationship to the existing stack

The layering remains:

```text
application or POSIX program
        |
filesystem.library / POSIX compatibility
        |
assign-aware VFS client and versioned storage protocol
        |
storage service core
        |
filesystem backend: ext4 | AstraFS | RAM | network
        |
synchronous block facade, when the backend is block based
        |
block lease and physical backend
```

`filesystem.library` owns process namespace behavior, assigns, path
qualification, logical symbolic-link traversal, and rights. The storage
service core owns protocol validation, sessions, generation-safe handles,
request concurrency, transport, and accounting. AstraFS owns only its mounted
volume, nodes, journal, cache, allocation policy, and on-disk bytes.

ext4 remains unchanged when the new VFS operations are introduced. Optional
features are advertised by each mounted filesystem rather than inferred from
the protocol version or service executable.

## Optional VFS capabilities

The VFS protocol version answers whether both peers understand an operation.
`AstraVfsFilesystemInfo.flags` answers whether the selected mounted filesystem
implements it. The initial capability set is:

```text
ASTRA_VFS_FEATURE_ATTRIBUTES
ASTRA_VFS_FEATURE_INDICES
ASTRA_VFS_FEATURE_QUERIES
ASTRA_VFS_FEATURE_LIVE_QUERIES
```

Capabilities are cumulative:

- indices require typed attributes;
- queries require indices; and
- live queries require ordinary queries.

The shipped ext4 profile advertises none of these capabilities and every new
operation returns `ASTRA_VFS_ERR_UNSUPPORTED`. That is a supported result, not
an emulation request or mount failure. Existing ext4 operations and volumes
retain their current behavior. If a future ext4 backend gains one capability,
it may advertise that capability without adopting the others.

The existing filesystem-information record has a flags field, so capability
advertisement does not enlarge that wire record. New operations append to the
operation number space and require a new negotiated VFS protocol version.
Existing request fields are not repurposed. Any operation that needs a wider
record receives an explicitly sized extension, following rename and symlink.

The private backend operation table may grow optional entries while it remains
an in-tree implementation seam. Before it becomes a public filesystem-service
kit, it must acquire an explicit table size, ABI version, and capability mask.
Per-volume filesystem information remains authoritative because mount mode and
format features can change what one backend instance supports.

Every capability requires both positive tests through a backend that supports
it and negative tests proving that ext4 reports and returns unsupported without
changing existing behavior.

## Typed attributes

An attribute belongs to one file object and consists of:

```text
UTF-8 name
stable type identifier
value length
value bytes
flags owned by the filesystem
```

The initial native types are deliberately small:

| Type | Ordering and validation |
|---|---|
| bytes | unsigned bytewise lexicographic order |
| UTF-8 string | valid UTF-8, bytewise lexicographic order |
| signed 64-bit integer | numeric signed order |
| unsigned 64-bit integer | numeric unsigned order |
| Boolean | false before true |
| nanosecond timestamp | signed numeric order |

Numeric values use one canonical big-endian encoding on the wire and disk.
Strings are validated but never case-folded, normalized, or locale-collated.
This matches Astra's native filename rule: storage is byte-exact and a caller
does not receive a different name than it supplied.

Attribute names use namespaces:

- `system.*` is derived or controlled by the filesystem and operating system;
- `user.*` is ordinary user metadata; and
- `app.*` is application-defined metadata.

Derived attributes such as name, size, kind, and modification time need not be
duplicated in attribute storage. AstraFS may expose them through reserved
`system.*` names and maintain their indices directly from inode changes.
Ordinary callers cannot overwrite a derived value.

The first VFS surface provides whole-value operations on an open file handle:

```text
ATTR_GET
ATTR_SET
ATTR_REMOVE
ATTR_LIST
```

Handle-relative access avoids path replacement races and keeps the existing
session authority model. `ATTR_SET` atomically creates or replaces one complete
value. Values and batched listings use the session's bound transfer area; the
transport-area maximum is the operation resource boundary rather than a
smaller filesystem constant. A listing returns name, type, flags, and value
length without reading each value.

The on-disk representation stores small attributes in available inode-local
space and spills larger values into ordinary allocated blocks. This distinction
is invisible to applications. Removing or replacing an attribute and updating
every affected index is one journal transaction.

The native typed API is authoritative. A future POSIX `getxattr` surface can
map byte-valued extended attributes onto it without weakening the native type
contract.

## Volume indices

An index is a volume-owned definition containing:

```text
attribute name
attribute type
ordering rules implied by that type
root block
index state
creation generation
```

The catalog is itself a checksummed B+tree. A ready index maps:

```text
(canonical typed value, generation-tagged inode ID) -> inode reference
```

The inode identity is part of the key so duplicate values are naturally
ordered and deleting then reusing an inode cannot make an old index entry name
a new object.

Index invariants are mandatory:

- the declared index type must match the attribute type;
- a file without that attribute has no entry;
- attribute replacement removes the old key and inserts the new key in the
  same transaction;
- inode deletion removes every entry for that inode;
- rename, size, and timestamp changes update indices over derived attributes
  in the same transaction; and
- a ready index never contains an entry that disagrees with the committed
  inode.

Index administration uses separate create, remove, list, and information
operations and requires filesystem-administration authority, not merely write
access to an arbitrary file.

The first correct online index builder may exclude filesystem writers while it
walks the volume. It records a non-queryable `BUILDING` catalog entry, builds
in bounded transactions, validates the result, and publishes `READY`
atomically. A crash leaves an index that mount recovery can discard or resume
but never query. Background building with a concurrent mutation log is an
upgrade only after measurements show that writer exclusion is unacceptable.

Index removal first makes the index non-queryable, then reclaims its blocks in
bounded transactions. Running out of space during either process cannot expose
a partially valid index.

## Structured queries

The first query contract is structured rather than a textual language. It maps
directly onto one ordered index traversal and avoids putting a general parser,
planner, and unbounded fallback scan in the filesystem service.

A query identifies:

```text
open directory handle that scopes authority
index name
comparison operator
typed key or key range
result flags
```

Initial operators are equality, less than, less than or equal, greater than,
greater than or equal, inclusive range, and prefix for byte and UTF-8 keys.
An absent or type-incompatible index returns a specific refusal. The service
does not silently scan a volume on the MC68040.

The wire operations are conceptually:

```text
QUERY_OPEN
QUERY_NEXT_AREA
QUERY_CLOSE
```

The service owns generation-safe query handles and releases them when their
session closes. Results are batched through the bound transfer area so one
result does not cost one cross-process round trip. Each result contains a
scope-relative path, informational stable object identity, basic node metadata,
and the matching typed key. Opening a result still uses the caller's directory
authority and ordinary `open_at` validation.

The initial query consistency model is committed and restartable rather than a
long-held snapshot. Each batch resumes strictly after its last index key and
inode identity and returns only entries verified against currently committed
inode metadata. Concurrent changes may cause a matching object to be omitted
or observed at its new position; no filesystem lock remains held while an
application thinks about the result. Applications that need continuous truth
use live queries. A future strict snapshot contract requires explicit metadata
snapshot support and is not implied by the first API.

General Boolean expressions, joins, locale collation, and automatic unindexed
scans are deferred. They may be added above the primitive range query when a
measured application requires them.

## Live queries

Live queries are a separate advertised capability and are not required for the
first writable format. A correct implementation must:

1. establish a subscription at a committed filesystem sequence;
2. enumerate the initial matching set without a notification gap;
3. publish committed additions, removals, and changes after that sequence;
4. identify objects by generation-tagged identity so consumers can reconcile
   rename and replacement; and
5. revoke every subscription and queued event when its session dies.

Notifications are published only after the corresponding journal transaction
commits. Aborted changes are never visible. The implementation reuses Astra's
event service and accounting rather than creating an independent unbounded
queue inside the filesystem.

## Network use

A future Astra network filesystem transports the same attribute, index, and
query operations at the object level:

```text
client filesystem.library
        |
local VFS and NetFS backend
        |
versioned authenticated network protocol
        |
remote VFS server
        |
remote ext4 or AstraFS backend
```

The server executes queries and returns bounded batches. It does not export
raw AstraFS blocks, journal records, allocator state, or backend pointers. An
ext4 export advertises ordinary file operations; an AstraFS export can also
advertise attributes, indices, queries, and live queries. Disconnect,
reconnection, replay, caching, and authentication are network-filesystem
policy and do not alter the local AstraFS disk format.

## On-disk format principles

The format uses 4 KiB logical blocks, matching the existing native ext4
profile, block facade, and VM page size. All fields have an explicit canonical
byte order and are encoded and decoded field by field. No compiler-native
structure, pointer, bitfield layout, C++ object, or host `errno` appears on
disk.

Ordinary block numbers are 32 bits. At 4 KiB per block this addresses 16 TiB
without imposing routine 64-bit block arithmetic on the MC68040. File sizes,
byte offsets, timestamps, transaction generations, and externally visible
object identities remain 64-bit where their semantics require it.

The superblock records a format major and minor plus three feature masks:

- an unknown compatible feature may be ignored;
- an unknown read-only-compatible feature permits a read-only mount; and
- an unknown incompatible feature refuses the mount.

Every variable record carries an explicit length. Structural maxima are
derived from the block size, record encoding, volume geometry, journal space,
or transfer-area capacity and are reported where callers need them. The format
does not impose guessed fixed counts for files, attributes, indices, extents,
or query results.

## Volume layout

The logical layout contains:

```text
boot-reserved region, if the image profile requires one
primary superblock
preallocated journal ring
allocation groups and their bitmaps/summaries
inode storage
directory, attribute, extent, and index B+trees
file data and out-of-line attribute data
backup superblock locations derived from volume geometry
```

The superblock identifies the volume UUID, geometry, feature masks, journal,
root inode, index catalog, allocation metadata, current clean generation,
checksum algorithm, and backup locations. Multiple checksummed superblocks
carry generations. Mount chooses only a self-consistent supported generation;
it does not merge fields from damaged copies.

The journal and the metadata space required to complete recovery are reserved
at format time and never handed to ordinary file allocation. This is a
correctness reserve derived from the largest atomic metadata operation and
tree height permitted by that volume, not an arbitrary user quota.

## Metadata block contract

Every metadata block begins with a common checksummed header containing at
least:

```text
format magic
metadata block type
header and record length
own physical block number
owning object or tree identity
transaction generation
checksum algorithm and checksum
```

Including physical and logical identity detects a valid block written to the
wrong location. Type-specific validators check record bounds, ordering,
ownership, generation, and child references before the block is trusted.

Metadata checksum failure prevents a writable mount or transitions a mounted
volume to read-only according to where the failure is discovered. The service
reports the exact object and block through observability and returns an I/O or
corruption status; it never follows unvalidated pointers in an attempt to
continue.

## Inodes and object identity

An inode records:

- inode slot and reuse generation;
- kind, mode, owner, group, and link count;
- byte size and allocated-block count;
- creation, modification, status-change, and access timestamps;
- inline extent records and an optional extent-tree root;
- inline payload shared by small file, symlink, and attribute representations;
- optional directory and attribute-tree roots;
- flags and checksum; and
- orphan-list state.

The externally meaningful object identity combines the inode slot and reuse
generation. A stale directory entry, index entry, query result, or open request
therefore cannot silently resolve to a newly created object that reused the
slot.

The exact inode size and division of inline space are selected from a measured
Astra installation corpus before the format is frozen. The design requires an
inline path and transparent spill, not an unmeasured byte count.

`.` and `..` are logical directory behavior and need not consume ordinary
directory records. Hard links refer to the same generation-tagged inode.
Short symbolic-link targets use inline payload; longer targets use ordinary
data extents.

## File data and extents

Files use logical-to-physical extents. A bounded number remain inline in the
inode and overflow uses the common B+tree implementation. Missing logical
ranges are sparse holes and read as zeroes without allocated disk blocks.
Newly allocated blocks are never exposed until their initialized data is
durable under the ordered journal protocol.

Allocation prefers blocks near the inode and previous extent while preserving
the ability to satisfy large contiguous requests. Delayed allocation,
preallocation, and online defragmentation are later policies and do not change
the extent format.

Small regular files may reside in inode-local payload. Crossing the inline
threshold allocates extents and migrates atomically; shrinking may return to
inline form only if measurement shows the extra transaction work is useful.

## Directories and the common B+tree

One checksummed B+tree implementation serves directories, attribute maps,
extent overflow, the index catalog, and volume indices. Type-specific key and
value codecs sit above the same split, merge, search, iteration, validation,
and journaling machinery.

Directory keys are exact UTF-8 name bytes. Values contain the target inode
identity and kind needed for safe lookup and listing. Tree blocks use slotted
records so variable names and attribute keys do not force fixed-width waste.
Every operation validates record offsets before comparing keys.

Tree mutation is journaled as complete replacement metadata blocks. Split and
merge preflight their journal and allocation requirements before publishing a
parent pointer. A failed operation leaves the previous tree reachable.

## Free-space management

The volume is divided into allocation groups with checksummed free-space
bitmaps and summaries. Summaries allow the allocator to reject unsuitable
groups without scanning every bitmap; the bitmap remains the authoritative
allocation state.

Allocation and free-space metadata are committed in the same transaction as
the reference that acquires or releases the blocks. A block is never both free
and reachable in a committed generation. Mount recovery replays committed
bitmap changes before normal allocation begins.

No ordinary allocation consumes the journal, superblocks, allocator metadata,
or recovery reserve. ENOSPC is reported before beginning an atomic operation
whose worst-case metadata cannot be committed. Delete, truncate, orphan
cleanup, journal checkpoint, and unmount retain the structural resources they
need to make progress under full-volume conditions.

## Journal and transaction protocol

AstraFS uses a physical metadata redo journal. A transaction contains:

```text
checksummed transaction header and sequence
descriptors naming complete destination metadata blocks
complete replacement metadata block images
checksummed commit record covering descriptors and payloads
```

The default durability mode is ordered data with metadata journaling:

1. reserve all data, metadata, journal, and recovery resources;
2. write newly allocated or modified file data;
3. flush the ordered data required by the transaction;
4. append checksummed metadata descriptors and block images to the journal;
5. flush the journal payload;
6. append and flush the commit record;
7. apply metadata blocks to their home locations; and
8. checkpoint the transaction and reclaim its journal span.

Only step 6 makes the transaction committed. Before it, recovery retains the
old namespace and metadata. After it, recovery replays the complete new
metadata. Replay is idempotent: applying one committed transaction any number
of times produces the same home blocks.

The block facade's flush completion is the durability boundary. A backend that
cannot provide ordered durable flush cannot mount AstraFS writable. Media
generation change, reset, or ambiguous completion aborts the active mount and
requires recovery before writes resume.

Recovery scans from the last checkpoint by transaction sequence. It accepts
only fully bounded records whose header, descriptor set, payload blocks, and
commit checksum agree. Scanning stops at an incomplete or invalid uncommitted
tail. A checksum-valid committed transaction with invalid destination or
structural metadata is corruption and refuses writable recovery rather than
being skipped.

Independent transactions may be grouped behind one flush while preserving
their individual commit records and order. The initial implementation uses one
metadata writer with concurrent readers. Parallel metadata writers, multiple
journals, and lock partitioning require measured contention and a new proof of
ordering; a single-core target receives no throughput merely from having more
locks.

An atomic operation must fit the journal space derived for the volume before
it mutates anything. Work that has no all-or-nothing external contract, such as
reclaiming an already unpublished index, may be split into bounded
transactions. Rename, link-count changes, attribute replacement plus index
updates, and publication of one index cannot be split across visible commits.

## Durability semantics

Successful namespace and write calls make changes committed in memory and
eligible for group commit; they do not individually promise physical-media
durability.

`fsync(file)` durably commits the file's prior data, size, extent map,
attributes, and relevant index entries. It does not by itself promise that a
new directory name survives unless that directory is also synchronized.
`fsync(directory)` durably commits prior namespace operations in that
directory. Filesystem sync commits all preceding operations and checkpoints
their metadata home writes.

Atomic rename, replacement, hard link, unlink, truncate, and attribute update
are each entirely old or entirely new after recovery. Ordered mode ensures a
committed extent never exposes data from a previous owner.

An open but unlinked inode enters a persistent orphan structure in the same
transaction that removes its final name. Last close reclaims it transactionally.
Mount recovery completes orphan cleanup before making the volume writable.

## Integrity and checksums

Superblocks, journal records, allocator metadata, inodes, and every B+tree
block have mandatory checksums. Checksums cover identity and generation as
well as payload bytes.

End-to-end regular-file data checksums are part of the intended format. The
checksum record is keyed by inode identity and logical range and is committed
with the extent metadata after the corresponding data reaches the medium.
Sparse holes have no checksum record and read as zeroes. A mismatch returns an
I/O error and reports the affected file and range. Detection does not imply
repair: without another copy the filesystem cannot reconstruct damaged user
data.

The initial checksum algorithm and granularity are selected only after target
measurements compare detection properties, MC68040 cycles, code size, metadata
overhead, and sequential I/O throughput. Generated code is inspected before
considering a shared assembly implementation. The on-disk algorithm identifier
allows a later incompatible or read-only-compatible format feature rather than
silently changing interpretation.

## Mount, failure, and corruption behavior

Mount performs, in order:

1. geometry and media-generation validation;
2. supported superblock generation selection;
3. feature and checksum validation;
4. journal scan and replay when dirty;
5. root, allocator, and index-catalog validation sufficient for safe access;
6. orphan cleanup; and
7. publication of the mounted namespace.

A dirty volume is not exposed before replay completes. A read-only mount may
inspect a dirty volume only through an explicit diagnostic mode that does not
pretend the pre-replay namespace is current.

Backend device error, reset, vanished media, or generation change invalidates
cached blocks and outstanding transactions. The service does not retry a write
against media whose identity may have changed. Mandatory metadata corruption
transitions an active writable mount to read-only, rejects unsafe operations,
and emits an observable event. Explicit offline repair remains separate from
normal mounting.

## Caching, concurrency, and performance

Correctness is independent of cache residency. Dirty metadata belongs to a
transaction and cannot be evicted as if it were clean. Cache invalidation is
generation-aware. Memory pressure may shrink clean cache without invalidating
open nodes or journal state.

Performance mechanisms expected in the first qualified implementation are:

- extent-based sequential I/O and read-ahead;
- allocation locality within groups;
- inline small files, symlinks, and attributes;
- B+tree lookup and `O(log n + returned results)` indexed queries;
- batched directory, attribute, and query replies through shared areas;
- cached validated metadata blocks;
- free-space summaries; and
- journal group commit.

The implementation establishes target baselines and regression budgets for
mount, replay, lookup, create, rename, sequential and random I/O, `fsync`,
attribute replacement, index maintenance, and query throughput before tuning
their hot paths. Each result records volume geometry, cache state, compiler,
image identity, host, and MC68040 environment.

Routine on-disk arithmetic stays 32-bit where the format permits it. Assembly
is used only after target profiles and generated-code inspection prove a
material improvement over the shared C implementation.

## Host tools and one format implementation

The filesystem is developed with these host tools:

```text
mkastrafs       create and describe a volume
fsck.astrafs    validate and explicitly repair a volume
dumpastrafs     inspect superblocks, transactions, objects, and trees
astrafs-journal inspect and replay journal state into a copy
astrafs-corrupt apply deterministic damage for tests
```

Target and host builds share canonical format codecs, checksum code, B+tree
validators, and journal parsing. `fsck.astrafs` nevertheless performs an
independent ownership and reachability walk; asking the mounted implementation
whether its own state is valid is not independent verification.

The checker validates at least:

- supported superblocks and generations;
- journal bounds, sequence, commit records, and replay result;
- one valid owner for every allocated block;
- no overlap among data, metadata, journal, and reserved regions;
- bitmap and ownership-walk agreement;
- inode generations, extents, sizes, and link counts;
- directory reachability and parent relationships;
- orphan state;
- B+tree ordering, bounds, child ownership, and sibling relationships;
- attribute record type and bounds; and
- exact agreement between every ready index and the committed attributes it
  represents.

Repair is explicit and records every decision. Normal mount never invokes a
repair policy automatically.

## Qualification gates

AstraFS is not eligible for persistent system data until all of these pass:

### Backend-neutral VFS

- positive attribute, index, query, and eventual live-query tests against a
  supporting in-memory reference backend;
- negative capability and operation tests against ext4;
- session death, stale handle, rights, subtree query scope, malformed record,
  and transfer-boundary tests; and
- native and POSIX callers continuing to pass the existing filesystem suite.

### Format and recovery

- deterministic crash injection after every block write and flush boundary of
  every mutating operation;
- replay followed by independent `fsck.astrafs` and comparison with the last
  acknowledged model state;
- torn, dropped, duplicated, and reordered writes consistent with the declared
  block backend contract;
- journal wrap, checkpoint, full journal, full volume, and recovery-reserve
  exhaustion;
- interruption during tree split, merge, index build, index removal, orphan
  cleanup, allocation, and truncation; and
- repeated replay proving idempotence.

### Corruption

- malformed and checksum-damaged copies of every metadata block type;
- wrong-block and wrong-owner metadata with otherwise valid payloads;
- cyclic, overlapping, out-of-range, stale-generation, and duplicate
  references;
- randomized image fuzzing under ASan, UBSan, and static analysis; and
- refusal or read-only containment without an invalid memory access or write
  outside the mounted image.

### Functional and concurrency

- sparse files, large extents, deep directories, hard links, symbolic links,
  open-unlinked files, atomic replacement, and all timestamp transitions;
- duplicate index keys, all typed orderings, missing attributes, online index
  build interruption, and query mutation races;
- concurrent readers and writers under the service's real locking model;
- clean unmount with no live allocations or unreclaimed handles; and
- remount and byte-exact content verification using host tools.

### Target and hardware

- clean MC68040 build and generated-code inspection of measured hot paths;
- target cycle, memory, and I/O budgets with automated regression thresholds;
- long-running stress in QEMU on Beast;
- repeated power-cut and storage-reset qualification on the DE25 path;
- media removal/generation-change behavior; and
- performance comparison with ext4 on the same image geometry, cache state,
  block backend, and workload.

Passing ordinary tests is not sufficient evidence for promotion. The release
record identifies the exact source, formatter, checker, image, QEMU, ROM,
backend, and hardware run.

## Staged implementation

Work proceeds as vertical, independently tested slices:

1. Define capability flags and whole-value typed attribute operations.
2. Prove positive behavior with an in-memory backend and negative behavior
   with unchanged ext4.
3. Freeze the common disk codecs, superblock selection, metadata headers, and
   deterministic image backend.
4. Implement allocation groups, inodes, directories, extents, and typed
   attributes with read-only host inspection.
5. Implement redo journaling, ordered writes, recovery, orphan cleanup, and
   explicit sync semantics before treating the image as writable.
6. Implement the common B+tree, index catalog, transactional maintenance, and
   structured range queries.
7. Add data checksums after the target algorithm measurement, then complete
   corruption, crash, and full-volume qualification.
8. Add live queries through the event service.
9. Add a network backend that transports the same object-level capabilities.
10. Consider system-volume use only after QEMU and DE25 release gates pass.

The first persistent end-to-end slice is intentionally narrow:

```text
format disposable image
mount
create file
set typed attribute
sync and unmount
remount and recover if required
read attribute
remove attribute
unmount
independently check image and allocation baseline
```

That slice must already use the final transaction, checksum, and validation
rules. A throwaway non-journaled writable format would test a different
filesystem and is not an accepted shortcut.

## Decisions that require measurement before format freeze

The architecture above is fixed enough to implement without guessing, but
these values remain deliberately undecided until target data exists:

- inode record size and division of inline payload;
- B+tree node record layout and split fill policy;
- allocation-group geometry and locality heuristic;
- journal size formula and group-commit timing;
- metadata and data checksum algorithm and data-checksum granularity;
- cache size and eviction policy under real memory pressure;
- read-ahead window and sequential-write aggregation; and
- thresholds for inline data, direct extents, and extent-tree promotion.

Each choice is recorded with the workload, before/after result, code-size cost,
memory cost, and recovery consequence. Disk-format choices are not selected
solely from host benchmarks.
