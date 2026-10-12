# Filesystem Kit

Status: `filesystem.library` ABI 12.0 is the shared, backend-neutral client layer
over Astra's VFS protocol, assign namespaces, and union policy. It does not
create a second filesystem stack.

## Native application contract

Applications link the direct symbols declared by the NDK. Their executable has
an eager `DT_NEEDED` dependency on `filesystem.library.12`; launch fails before
`main()` with a precise diagnostic when the required ABI is unavailable. An
`AstraFilesystem` attaches to the process namespace already built from its
startup grants; it borrows those assign and client objects and cannot expand
their rights. A client connected to a provider-enforced grant
(`ASTRA_VFS_FEATURE_GRANTS`) narrows it with `astra_vfs_grant_derive`: a
sub-root, read-only, non-delegable, or a mix, never wider
(docs/FILESYSTEM_PRODUCTION_PRD.md FS15).

The API provides assign-aware open/close, sequential and positioned I/O,
64-bit seek, sync, truncate, stat and lstat, chmod, mkdir, unlink, atomic
rename, readlink, symlink, bounded directory batches, and path qualification.
Creation modes are explicit. Directory entries carry metadata in the listing
response so `ls -l` does not pay another service round trip per entry.

The same versioned library exports the VFS client and assign primitives needed
by filesystem services, diagnostic tools, and compatibility runtimes.
Applications never see ext4 objects, backend errno values, block sizes, mount
internals, or on-disk formats.

Path and open-file metadata include one `AstraVfsNodeMeta` record. Its validity
bits distinguish unknown fields from real zeros; identity comes from the
provider and agrees across bindings. It also carries allocation, full-width
link counts, and nanosecond timestamps. The compact batch-directory wire
format reports unknown extended metadata.

## Open descriptions

HostFS advertises `ASTRA_VFS_FEATURE_DESCRIPTIONS`. Its provider owns each
open description's position, directory cursor, append and nonblocking status.
Sequential read and write advance that position in one provider transaction;
positioned I/O leaves it unchanged, and positioned writes ignore append.
Seek and `astra_filesystem_file_flags` query or update the same description.
A short sequential transfer is returned to the caller rather than split into
several independently interleavable operations.

`astra_process_file_share`, `adopt` and `unshare` transfer a pending provider
reference through a one-use receipt bound to the original grant. Fork adopts
child-owned handles before the parent returns; closing either process's
handle preserves the other. Owner death drops unclaimed receipts. An in-place
personality exec exports handles without reopening paths. The POSIX adapter
also preserves child wait handles and resource accounting across exec.
Backends without this feature retain local positions and refuse description
sharing with UNSUPPORTED.

Process clients have stable addresses even when their namespace cache grows;
open files may safely borrow a client through later mount connections.

## Name and link semantics

Symbolic links live in Astra's logical namespace, above individual filesystem
backends. The shared assign layer owns traversal for every native and POSIX
caller:

- `stat` and ordinary opens follow the final link; `lstat` and `readlink`
  inspect it. A trailing slash requires a directory. Mutation operations
  inspect the final entry and reject a final symlink with a trailing slash.
  `mkdir` reports an existing final entry as EXISTS;
- relative targets are resolved from the link's logical parent;
- absolute targets use Astra's `/assign/path` form and must name an assign the
  process actually holds;
- components, including `..`, are resolved in order. Leaving a provider root
  continues in the process namespace; its virtual root is its own parent;
- rights are checked again after traversal, against the resolved assign; and
- cycles and more than 40 followed links return `ASTRA_VFS_ERR_LOOP`
  (`ELOOP` in POSIX), with one budget across provider and namespace hops;
- the first entry in a union owns a symlink, including a dangling one.

HostFS resolves inside each grant with the canonical provider walk. An escape
returns its complete unresolved namespace continuation, which the Kit checks
against the caller's grants. Other backends expose no-follow primitives and
use the same Kit walker. No backend interprets assign names or grants itself
authority to cross into another filesystem.

Path and directory-relative removal share one contract: zero flags removes a
non-directory entry; `ASTRA_VFS_UNLINK_DIRECTORY` removes an empty directory.
The provider checks the kind as part of removal. Both operations retain ordered
path resolution, trailing-slash requirements, and protected namespace roots.

## POSIX compatibility boundary

The implemented POSIX library maps integer descriptors and errno onto the same
native operations:

| POSIX surface | Filesystem Kit primitive |
| --- | --- |
| `open`, `close`, `read`, `write` | `open_mode`, `close`, `read`, `write` |
| `pread`, `pwrite`, `lseek` | `read_at`, `write_at`, `seek` |
| `stat`, `lstat`, `fstat` | `stat`, `lstat`, `file_info` |
| `fsync`, `ftruncate`, `chmod` | `sync`, `truncate`, `chmod` |
| `mkdir`, `unlink`, `rmdir`, `rename` | `mkdir_mode`, `unlink` with kind flags, `rename` |
| `readlink`, `symlink` | `readlink`, `symlink` |
| `opendir`, `readdir` | `directory_open`, `directory_read` |

Descriptor tables, current-directory presentation, cancellation, and errno
translation remain POSIX policy and do not leak into the native library.

## Filesystem service boundary

`AstraVfsBackendOps` is the private implementation seam shared by today's
storage, host, and logical RAM-volume services. The service core owns protocol decoding, validation, sessions,
handles, generations, concurrency, cancellation, and accounting. A backend
owns filesystem nodes and medium access, returns Astra VFS statuses, provides
its own filesystem locking, and never retains request pointers. ext4, hostfs,
and ramfs implement the same operation table under `vfs/src/backends/`.
Service definition/publication and lifecycle management already use the NDK
Service Manager; a second device or mount registry would duplicate that
authority. A logical RAM volume has no raw block endpoint to register.

A public filesystem-service kit should expose this split as a versioned server
contract only when an out-of-tree filesystem is ready to consume it. That kit
will package mount lifecycle and the service runner around a backend operation
table; it will not expose the current private structure verbatim or force a
block-device model on network filesystems. This is the minimum stable boundary
needed for plug-in filesystems without freezing unfinished SMB/NFS policy now.

## Installation and versioning

The image builder installs the MC68040 library in the provider image at:

```text
/libs/Filesystem.kit/libraries/filesystem.library/abi-12/12.0.0/m68k-68040/filesystem.library
```

The Kit is version 9.0.0 and provides `filesystem.library` 12.0.0. Other ABI
majors and versions live beside it. Programs name the SONAME at link time and
package metadata records the compatible minimum; programs never construct
provider paths.

The image builder derives `/libs/.providers/filesystem.library.abi-11` from the
authoritative manifest and the library's embedded identity. Kit builds start
from an empty bundle, so removed versions and undeclared stale payloads cannot
survive an incremental rebuild.

Only the supervisor bootstrap loader talks directly to VFS before it can load
`filesystem.library`; filesystem services and the library implementation use
the protocol directly by definition. All other application-facing filesystem
behavior belongs in the shared library.

## Deferred AstraFS extension

The future native AstraFS design extends this same Kit and protocol with
per-filesystem optional capabilities for typed attributes, volume indices,
structured queries, and live queries. ext4 remains a conforming backend by
advertising none of those capabilities and returning unsupported for their
operations. The proposed semantics, disk architecture, recovery contract, and
qualification gates are recorded in [ASTRAFS.md](ASTRAFS.md). No part of that
deferred design is implemented or included in the current Kit ABI.

The process VFS owns current directory identity. Process facades accept relative
paths against that retained directory; rename does not reopen its old spelling.
Fork shares the directory description and exec restores it in native VFS state.
`astra_process_vfs_current_directory` returns its current native slash path or
NOT_FOUND after removal or escape from its grant. Explicit directory-anchored
APIs support mutations and readlink; STAT_AT selects final-link following with
ASTRA_VFS_STAT_FOLLOW. These layout and signature changes require Filesystem
Kit ABI 12.0 and filesystem.library.12.

Provider loss returns `ASTRA_VFS_ERR_LOST` (POSIX `ESTALE`). Old files, open
directories and CWD remain disconnected; fork can inherit them without
reopening their former names. STOR 34 resumes the existing HELLO control
session to refresh a lost scope. After a provider reset, new absolute opens
can recover through surviving grant ports, provided the grant root's volume,
inode and generation still match. Absolute chdir can replace a lost CWD.
HostFS death permanently loses its old grants. Supervisor restarts affected
managed services after their namespace publications return; applications
need a new launch.

## Atomic write bounds

ABI 12 requires an exclusive `max_end` byte position on sequential and
positioned writes, including their accelerated callbacks. `UINT64_MAX` means
unbounded. Providers select the current position or append EOF and check the
bound within the same transaction as the write. A request crossing the bound
returns a short write; a nonempty request starting at or beyond it returns
`ASTRA_VFS_ERR_FILE_TOO_LARGE` (status 22) without moving the position. Zero
writes succeed without changing size or position. Positioned writes ignore
the open description's APPEND flag.

STOR 35 introduced `AstraVfsRequest.write_limit` for inline and shared-area
writes; Host transport `0x00010015` carries the bound in WRITE's input value
and returns the resulting position there. Configuration version 4 is unchanged.
HostFS serializes writes and truncations with its existing namespace lock,
including writes through independent descriptions. RAMFS uses its filesystem
lock; ext4 selects position, clamps length and writes under its mount lock.
The POSIX adapter supplies `RLIMIT_FSIZE`, maps status 22 to `EFBIG`, and sends
`SIGXFSZ` only when a finite bound refuses all bytes. Limit and signal policy
remain in POSIX; providers implement the generic bound once.

STOR 36/minimum 36 makes ordered LOOKUP accept a retained directory token.
Filesystem Kit ABI 12.0 adds the attached namespace as the first argument
of path-based `astra_filesystem_*_at` APIs. Provider continuations retain
the same 40-link budget and use the shared namespace resolver; POSIX adapters
call that native contract. Joined-assign opens use the member selected by
lookup, including dangling-link shadowing. Rename/link retain namespace
escapes instead of conflating them with a cross-device operation.
