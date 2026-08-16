# Filesystem Kit

Include `astra/filesystem_kit.h` for the logical shared-library name and
minimum ABI accepted by `OpenLibrary()`. Include
`astra/filesystem_library.h` from the Filesystem Kit development package for
the typed ABI 1.0 export table.

The library attaches to the process namespace already created from startup
grants. It supports assign-aware files, explicit-offset I/O, 64-bit seeks,
metadata, mutation, and bounded union-directory enumeration. It also exports
the underlying VFS client operations for filesystem and compatibility tools.
The ABI has no dependency on lwext4 or any other on-disk implementation; RAM
drives and future filesystem services use the same protocol and API.

The native ABI is intentionally not a libc ABI. A future POSIX Kit will add
file descriptors, current-directory and root rules, `errno`, and libc wrappers
over these same operations. See the repository's `docs/FILESYSTEM_KIT.md` for
the compatibility map and current service limits.
