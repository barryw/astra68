# Astra universal shared-library format

**Status:** LOCKED direction for format version 2. Version 1 remains the
implemented transition format until all in-tree libraries and the loader move
together.

## 1. One format

Astra has one shared-library species: `.library`. There is no parallel `.so`
namespace. Native and POSIX programs use the same C ABI, files, dependency
resolver, mapped code pages, and package identities. "POSIX" describes an API
surface, not a binary format.

The target linker resolves ordinary `-lNAME` requests to the compatible
`NAME.library` selected from the package/NDK dependency closure. No fake `.so`
links or implementation-bearing import archives are installed. Native code may
include a POSIX library header; POSIX code may include an Astra NDK header.

## 2. File and identity

A `.library` is big-endian ELF32/m68k `ET_DYN` code for MC68040. It carries:

- the existing fixed `ALIB` identity record, advanced to record version 2;
- semantic version, library ABI major/minor, architecture, and build identity;
- `DT_SONAME`, explicit `DT_NEEDED` dependencies, dynamic symbols and strings;
- versioned public C symbols and eager relocation records;
- optional `.init_array`, `.fini_array`, and TLS metadata; and
- no embedded package search paths or host filesystem paths.

The package manifest supplies compatible version constraints. Installation
resolves an exact version and digest. A process records that exact closure;
runtime lookup never silently substitutes a different compatible file.

Default symbol visibility is hidden. A library definition maintained with its
public NDK headers names the exported symbols and their ABI versions. The build
generates the ELF export/version data and compatibility report from that one
definition; it does not infer an ABI by scraping arbitrary C headers.

## 3. Loading and relocation

Normal dependencies are loaded eagerly before `main()`. The runtime maps the
complete acyclic closure, validates every identity, resolves versioned symbols,
applies relocations, establishes TLS, seals RELRO pages, and then runs
constructors in dependency order. Destructors run in reverse order.

Lazy binding, `LD_PRELOAD`, current-directory lookup, writable executable code,
text relocations, copy relocations, unresolved symbols, and symbol resolution
outside the declared closure are forbidden. Function/GOT binding is eager.
Writable globals and TLS are private to each process; executable and read-only
pages are shared by exact content identity.

The kernel validates and maps pages but does not implement a Unix dynamic
linker or search the filesystem. Dependency resolution, symbol binding, and
constructor policy remain in the versioned user runtime and loader service.

`OpenLibrary()` remains the API for optional dependencies, plugins, and runtime
discovery. It opens the same `.library` files and exact identities used by
normal linking. Existing typed export tables may be retained as public APIs,
but they are views of the universal library rather than a second ABI species.

## 4. Toolchain and packaging

The Astra GCC/binutils target, NDK, package builder, and loader share one
library-definition source of truth. A library build emits one `.library`, its
detached symbols, public headers, ABI report, and Kit/package metadata. ABI
checks reject removed or changed symbols within an ABI major. A major break is
installed beside the previous major.

Foundational C, math, C++, threading, and POSIX facilities may be delivered as
`.library` implementations where sharing is beneficial. A tiny startup and
relocation runtime remains statically linked so dependency loading can bootstrap
without depending on itself.
