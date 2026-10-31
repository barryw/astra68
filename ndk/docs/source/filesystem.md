# Filesystem Kit

Include `astra/filesystem_kit.h` for the complete API. Link against
`filesystem.library.15` and declare the minimum compatible version in the
bundle manifest. The process loader resolves that dependency before `main`;
applications call the declared `astra_filesystem_*` functions directly.

The library attaches to the process namespace already created from startup
grants. It supports assign-aware files, explicit-offset I/O, 64-bit seeks,
metadata, mutation, and bounded union-directory enumeration. It also exports
the underlying VFS client operations for filesystem and compatibility tools.
The ABI has no dependency on lwext4 or any other on-disk implementation; RAM
drives and other filesystem services use the same protocol and API.

ABI 15.8 adds path-scoped saved search presets. `astra_saved_query_save`
creates a private store directory when needed, writes a versioned file, syncs
it, and publishes it without replacing an existing preset. `load` validates
the complete file; `snapshot_open` enumerates published presets and skips
malformed entries. Presets carry label, directory, and case-insensitive name
substring. Applications own presentation and may reuse the same store.
`astra_saved_query_set_label` changes a preset's display name through a synced
temporary file and atomic rename, preserving its filename and search. It
checks the caller's snapshot against the current record before updating.

ABI 15.9 adds `astra_recent_item_record` and owned recent-item snapshots.
Applications record successfully opened files, folders, and bundles in one
private HOME store; the library owns synced publication, newest-first ordering,
duplicate path collapse, and the 64-event retention limit. Each record carries
an exact volume reference for removable media. Applications check that reference
against the current volume catalog before presenting or reopening a result.
Missing stores return an empty snapshot; malformed records are skipped.
`astra_recent_item_snapshot_close` releases snapshot storage. Saved searches and
recent items share internal document I/O and a target stack-frame gate, so
client code never implements their wire formats or write protocol.

ABI 15.10 adds `astra_filesystem_rename_noreplace` and the corresponding VFS
client operations. They publish a rename only when the destination remains
absent. Filesystem Kit tree copy and move use this atomic operation, so a
concurrent creator cannot have its destination replaced after the copy's
initial existence check.

The native ABI is not a libc ABI. `libc.library.4` provides the POSIX file
API -- descriptors, the current directory, `errno`, and stdio -- over these
same operations; a POSIX program uses it instead. See the repository's
`docs/FILESYSTEM_KIT.md` for the compatibility map and current service
limits.

Version 9 preserves dot components and trailing separators until ordered
lookup. Links use `/assign/path` for absolute targets; a provider escape
continues only through assigns held by the process. One 40-link budget covers
the complete lookup. Final-link behavior follows the operation, and rejected
mutations do not act on a final symlink's target.

`AstraFileInfo.meta` and the metadata from stat and lookup include provider
identity, allocation, full-width link counts, and nanosecond timestamps.
`meta.valid` holds `ASTRA_VFS_META_*` bits; a clear bit means unknown and a
set bit makes zero an ordinary value. Volume and inode identities agree
across bindings and hard links. Generation is the node's birth time when
available. Batched directory entries report unknown extended metadata.
Path and directory-relative unlink use zero flags for a non-directory, or
`ASTRA_VFS_UNLINK_DIRECTORY` for an empty directory. The provider checks the
entry's kind during removal, and paths use the same ordered namespace walk.

Storage protocol 38 is the minimum accepted version. Its fixed 256-byte
control request references complete paths in the bound transfer area; rename
and link carry both complete operands there. Providers snapshot these bytes
before resolution. There is one wire form, including for short paths.
Components accept 255 UTF-8 bytes and native paths accept 4095 bytes, excluding
the terminating zero. Longer components or paths return
`ASTRA_VFS_ERR_NAME_TOO_LONG` (native status 23); POSIX maps it to
`ENAMETOOLONG`. Metadata and continuation payloads retain full-width volume
references. Existing clients retain their ABI 15.0 or 15.1 minimum; Disk Setup
requires 15.2.

ABI 14.0 adds the native volume catalog, complete owned snapshots, subscriptions
and exact-incarnation root grants. `VOLUMES` conveys metadata only; distinct
`VOLUME_READ` and `VOLUME_WRITE` brokers admit public mounted roots. System and
private roots retain their explicit startup grants. A catalog snapshot publishes
no partial result when its sequence changes between pages. Close snapshot and
root owners after use. Network mounting is not part of this implementation.

ABI 14.1 adds `astra_volume_removal_advance` for clients that observe an
admitted unmount. It separates pending cleanup, refusal, retained lost objects,
provider retirement and actual completion, and releases an image source only
after its mount retires. `LOST` does not prove that retained backing is gone;
completion waits for the source record to disappear.

ABI 15.0 publishes catalog version 5. Physical device and source records
include partition map, number, byte offset, capacity and detected type; the new record size
requires every producer and consumer to use the new library and headers.
`ASTRA_VOLUME_PHYSICAL_READ_ONLY` reports hardware/media protection separately
from effective `ASTRA_VOLUME_READ_ONLY`; `ASTRA_VOLUME_FORMATTING` prevents
mount admission while an authorized disk edit owns a source.
`astra_volume_snapshot_find` resolves one exact epoch/ID/generation;
`astra_volume_snapshot_ancestor` follows exact parent generations to a
requested kind. Missing, replaced and cyclic chains return `NULL`. Keep the
snapshot open while using returned record pointers. Existing catalog symbols
use `ASTRA_FILESYSTEM_15.0`.

ABI 15.1 exports the following two operations under `ASTRA_FILESYSTEM_15.1`.
Disk Setup alone receives
`DISK_ADMIN`. `astra_volume_format` admits
FAT32 or exFAT formatting for an exact unmounted physical source, with an
ASCII label of 1–11 characters. The call is asynchronous; keep the original
source reference and poll `astra_volume_format_status` until its outcome stops
returning `ASTRA_ERROR_BUSY`. The source generation changes at completion and
the catalog reprobes it before the new filesystem becomes available. System,
write-protected, mounted and stale sources are refused. `VOLUME_ADMIN` cannot
format disks.

ABI 15.2 exports `astra_volume_partition`,
`astra_volume_partition_status` and `astra_volume_partition_find_free` under
`ASTRA_FILESYSTEM_15.2`. Catalog version 6 reports
`ASTRA_VOLUME_PARTITIONING`. Select an exact physical device; initialize an
MBR/GPT map, create a FAT32/exFAT/ext4 partition in a byte-aligned extent, or
delete an exact partition number. A creation only sets the partition type;
format the resulting source separately. `find_free` uses a coherent snapshot
and reserves one MiB at each disk edge. Unmount every sibling partition before
editing. Keep the original device reference and poll status until it stops
returning `ASTRA_ERROR_BUSY`; successful reread publishes a new generation.
Only `DISK_ADMIN` admits map edits. Physical DE25 edit and rediscovery gates
remain open in the current engineering state.

HostFS keeps the position, directory cursor, append and nonblocking flags in
one shared provider description. Sequential I/O advances its position;
positioned I/O preserves it and positioned writes ignore append. Use
`astra_filesystem_file_flags` to query or change APPEND/NONBLOCK. Fork shares
that description through one-use, grant-bound receipts and waits for child
adoption before returning. Closing or exiting either owner leaves adopted
handles alive. An in-place exec retains files and open directory descriptors
unless marked close-on-exec, plus child wait handles and resource accounting.
Providers without shared descriptions
return UNSUPPORTED when asked to share one.
Native file, directory and borrowed-reader objects carry an ownership epoch.
In a fork child, copied objects return LOST; receivers obtain current owners
through native receipt adoption. Closing a copy releases its local inherited
pin; it does not
close the parent's description. These pins keep borrowed clients alive through
namespace refresh. POSIX fork adopts its descriptors and cwd through the same
native mechanism. Multi-threaded fork is refused by the kernel before clone and
maps to `ENOTSUP` in POSIX.

The process VFS owns current directory identity. Process facades accept relative
paths against that retained directory; rename does not reopen its old spelling.
Fork shares the directory description and exec restores it in native VFS state.
Exec state version 12 owns variable-length root and current-directory strings;
it preserves the same complete native path bounds across handoff.
`astra_process_vfs_current_directory` returns its current native slash path or
NOT_FOUND after removal or escape from its grant. Explicit directory-anchored
APIs support mutations and readlink; STAT_AT selects final-link following with
ASTRA_VFS_STAT_FOLLOW. These layout and signature changes require Filesystem
Kit ABI 14.0 and filesystem.library.14.

Path-based `astra_filesystem_*_at` calls take the attached `AstraFilesystem`
first, then the retained directory and relative path. Ordered provider LOOKUP
also accepts a directory token. A namespace escape continues from that object
through the shared assign resolver, with its consumed links charged to the same
40-link budget. Native calls and POSIX `openat`/`fstatat` therefore share routing.

Provider loss returns `ASTRA_VFS_ERR_LOST`; POSIX reports `ESTALE`. Old file,
directory and CWD descriptions stay lost even when the same pathname exists
again. Fork can inherit a disconnected description. A provider reset preserves
grant ports: the Kit refreshes the scope through HELLO while retaining its
control session, and new absolute opens can succeed. A grant root is recovered
only when its volume, inode and generation still match. Absolute chdir can
replace a lost CWD; relative operations cannot silently reopen its spelling.
When HostFS dies, its old grants remain lost. Supervisor restarts affected
managed services with new grants; applications need a new launch.

## Atomic write bounds

ABI 12 retains the exclusive `max_end` byte position on sequential and
positioned writes, including their accelerated callbacks. `UINT64_MAX` means
unbounded. Providers select the current position or append EOF and check the
bound within the same transaction as the write. A request crossing the bound
returns a short write; a nonempty request starting at or beyond it returns
`ASTRA_VFS_ERR_FILE_TOO_LARGE` (status 22) without moving the position. Zero
writes succeed without changing size or position. Positioned writes ignore
the open description's APPEND flag.

STOR 35 introduced `AstraVfsRequest.write_limit` for inline and shared-area
writes; Host transport `0x00030000` carries the bound in WRITE's input value
and returns the resulting position there. Host channel configuration 5 carries
the kernel-stamped native process identity separately from resource ownership.
HostFS serializes writes and truncations with its existing namespace lock,
including writes through independent descriptions. RAMFS uses its filesystem
lock; ext4 selects position, clamps length and writes under its mount lock.
The POSIX adapter supplies `RLIMIT_FSIZE`, maps status 22 to `EFBIG`, and sends
`SIGXFSZ` only when a finite bound refuses all bytes. Limit and signal policy
remain in POSIX; providers implement the generic bound once.

Ordered LOOKUP accepts a retained directory token.
Filesystem Kit ABI 14.0 retains the attached namespace as the first argument
of path-based `astra_filesystem_*_at` APIs. Provider continuations retain
the same 40-link budget and use the shared namespace resolver; POSIX adapters
call that native contract. Joined-assign opens use the member selected by
lookup, including dangling-link shadowing. Rename/link retain namespace
escapes instead of conflating them with a cross-device operation.
