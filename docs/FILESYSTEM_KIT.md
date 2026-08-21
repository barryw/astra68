# Filesystem Kit

Status: `filesystem.library` ABI 1.0 is implemented as the shared client layer
over Astra's existing VFS, assign, union, and service protocols. It does not
create a second filesystem stack.

## Native contract

Applications open `filesystem.library` through `OpenLibrary()` and use its
typed `AstraFilesystemLibraryV1` export table. An `AstraFilesystem` attaches to
the process namespace that the runtime already built from its startup grants.
The attachment borrows those assign and client objects; it neither owns nor
duplicates their service sessions.

The ABI is backend-neutral. It exposes no ext4 structures, backend error
numbers, block sizes, or on-disk formats. Any service that implements the VFS
protocol can back an assign member, so disk volumes, RAM drives, network
filesystems, and mixed union assigns use the same application API.

The high-level API provides:

- assign-aware open and close;
- sequential and explicit-offset reads and writes;
- 64-bit seek and file information;
- path stat, directory creation, and unlink;
- bounded directory batches across every member of an assign union, retaining
  the member number for each entry; and
- path qualification for shell- and application-style current directories.

The low-level portion of the same export table exposes the existing VFS client,
assign-resolution, and union-open primitives. Filesystem handlers, diagnostic
tools, and compatibility runtimes can therefore use the protocol directly
without applications depending on private service structures.

All operations are synchronous and bounded. The caller owns context, file, and
directory objects and must close live files before detaching. Process teardown
remains the hard cleanup boundary for service handles and shared libraries.

## POSIX compatibility boundary

The native API deliberately retains Astra assigns, capability-derived rights,
typed status values, and explicit objects. A POSIX compatibility library will
layer process-local state over it:

| POSIX surface | Filesystem Kit primitive |
| --- | --- |
| `open` / `close` | `open` / `close` |
| `read` / `write` | stateful `read` / `write` |
| `pread` / `pwrite` | `read_at` / `write_at` |
| `lseek` | 64-bit `seek` |
| `stat` / `fstat` | `stat` / `file_info` |
| `mkdir` / `unlink` | `mkdir` / `unlink` |
| `opendir` / `readdir` | `directory_open` / bounded `directory_read` |

That layer will own integer file-descriptor tables, current-directory rules,
POSIX path presentation, `errno` translation, and libc cancellation behavior.
Those policies do not belong in the native filesystem library.

ABI 1.0 does not pretend the service already supports rename, links, metadata
mutation, timestamps, locking, truncate, or durable-sync operations. Each lands
once in the VFS protocol and backend first, then appears in this export table,
then receives its POSIX mapping. This keeps native and compatibility behavior
from silently disagreeing.

## Installation and versioning

The image builder installs the MC68030 library at:

```text
LIBS:Filesystem.kit/libraries/filesystem.library/abi-1/1.0.0/m68k-68030/filesystem.library
```

Other ABI majors and versions live beside it. Applications request the minimum
compatible ABI through `OpenLibrary()`; they do not construct or search these
paths themselves.

The image builder also derives `LIBS:.providers/filesystem.library.abi-1` from
the authoritative Kit manifest and embedded library identity. After the first
successful load Axiom retains the exact identity and initial pages, so later
processes attach without searching or reading the library file. Read-only
pages are shared and writable pages remain process-private.

Terminal, `events`, and `which` now use this interface for filesystem work.
Only the supervisor's bootstrap loader talks to the VFS client directly,
because it must read `filesystem.library` before that library can be opened;
filesystem services and the library implementation remain direct protocol
code by definition.
