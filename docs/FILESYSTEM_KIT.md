# Filesystem Kit

Status: `filesystem.library` ABI 1.5 is the shared, backend-neutral client layer
over Astra's VFS protocol, assign namespaces, and union policy. It does not
create a second filesystem stack.

## Native application contract

Applications open `filesystem.library` through `OpenLibrary()` and use its
typed `AstraFilesystemLibraryV1` export table. An `AstraFilesystem` attaches to
the process namespace already built from its startup grants; it borrows those
assign and client objects and cannot expand their rights.

The API provides assign-aware open/close, sequential and positioned I/O,
64-bit seek, sync, truncate, stat and lstat, chmod, mkdir, unlink, atomic
rename, readlink, symlink, bounded directory batches, and path qualification.
Creation modes are explicit. Directory entries carry metadata in the listing
response so `ls -l` does not pay another service round trip per entry.

The low-level portion of the same export table exposes the VFS client and
assign primitives needed by filesystem services, diagnostic tools, and
compatibility runtimes. Applications never see ext4 objects, backend errno
values, block sizes, mount internals, or on-disk formats.

## Name and link semantics

Symbolic links live in Astra's logical namespace, above individual filesystem
backends. The shared assign layer owns traversal for every native and POSIX
caller:

- `stat`, open, and ordinary path operations follow the final link; `lstat`,
  `readlink`, `unlink`, and replacement rename inspect the link itself;
- relative targets are resolved from the link's logical parent;
- absolute targets use Astra's `ASSIGN:path` form and must name an assign the
  process actually holds;
- normalization rejects attempts to escape above an assign root;
- rights are checked again after traversal, against the resolved assign; and
- cycles or more than 40 traversals return `ASTRA_VFS_ERR_LOOP` (`ELOOP` in
  POSIX). Forty is the established Unix traversal ceiling, not a storage-size
  limit.

Backends expose no-follow `stat`/`readlink`/`symlink` primitives. They do not
interpret assigns, follow cross-filesystem targets, or make capability
decisions. This keeps ext4, a future RAM filesystem, SMB, and NFS under one
namespace and security policy.

## POSIX compatibility boundary

The implemented POSIX library maps integer descriptors and errno onto the same
native operations:

| POSIX surface | Filesystem Kit primitive |
| --- | --- |
| `open`, `close`, `read`, `write` | `open_mode`, `close`, `read`, `write` |
| `pread`, `pwrite`, `lseek` | `read_at`, `write_at`, `seek` |
| `stat`, `lstat`, `fstat` | `stat`, `lstat`, `file_info` |
| `fsync`, `ftruncate`, `chmod` | `sync`, `truncate`, `chmod` |
| `mkdir`, `unlink`, `rename` | `mkdir_mode`, `unlink`, `rename` |
| `readlink`, `symlink` | `readlink`, `symlink` |
| `opendir`, `readdir` | `directory_open`, `directory_read` |

Descriptor tables, current-directory presentation, cancellation, and errno
translation remain POSIX policy and do not leak into the native library.

## Filesystem service boundary

`AstraVfsBackendOps` is the private implementation seam used by today's
storage service. The service core owns protocol decoding, validation, sessions,
handles, generations, concurrency, cancellation, and accounting. A backend
owns filesystem nodes and medium access, returns Astra VFS statuses, provides
its own filesystem locking, and never retains request pointers. lwext4 and the
in-memory tests already implement the same operation table.

A public filesystem-service kit should expose this split as a versioned server
contract only when an out-of-tree filesystem is ready to consume it. That kit
will package mount lifecycle and the service runner around a backend operation
table; it will not expose the current private structure verbatim or force a
block-device model on network filesystems. This is the minimum stable boundary
needed for plug-in filesystems without freezing unfinished SMB/NFS policy now.

## Installation and versioning

The image builder installs the MC68030 library at:

```text
LIBS:Filesystem.kit/libraries/filesystem.library/abi-1/1.5.0/m68k-68030/filesystem.library
```

The Kit is version 1.4.0 and provides `filesystem.library` 1.5.0. Other ABI
majors and versions live beside it. Programs request a minimum compatible ABI
through `OpenLibrary()`; they never construct provider paths.

The image builder derives `LIBS:.providers/filesystem.library.abi-1` from the
authoritative manifest and the library's embedded identity. Kit builds start
from an empty bundle, so removed versions and undeclared stale payloads cannot
survive an incremental rebuild.

Only the supervisor bootstrap loader talks directly to VFS before it can load
`filesystem.library`; filesystem services and the library implementation use
the protocol directly by definition. All other application-facing filesystem
behavior belongs in the shared library.
