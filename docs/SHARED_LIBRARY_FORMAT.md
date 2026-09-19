# Astra universal shared-library format

**Status:** IMPLEMENTED format version 2. In-tree libraries, commands,
applications, and ordinary services use the eager dynamic profile. Automatic
dependency closure, versioned binding, per-thread TLS, constructors,
destructors, reversible mapping, and kernel-enforced GNU RELRO are part of the
validated loader contract.

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

Every normal dynamic executable carries `PT_INTERP` naming
`loader.library.1`. On Astra that field is a versioned library identity, not a
host path. Package installation atomically selects one exact provider for each
`(name, ABI major)` and publishes an immutable `LIBS:.providers` record. The
supervisor resolves only the interpreter through that record; the interpreter
resolves the program's complete `DT_NEEDED` closure through the same VFS
resolver. Neither path scans Kits or chooses a version at launch. The kernel
maps the already-validated loader image and enters it with the program entry
preserved in startup state. The loader closes its temporary filesystem
capability before application code runs and then enters the common CRT.

Axiom validates three distinct ELF roles. Programs are `ET_EXEC`; ordinary
libraries are `ET_DYN` with a zero entry and may carry one immutable TLS
template; the interpreter is `ET_DYN` with a real executable entry and may not
carry TLS because establishing the process TLS layout is part of its bootstrap
job. Libraries and the interpreter cannot recursively name another
interpreter. All three retain the same strict W^X and eager-binding rules.

The kernel validates and maps pages but does not implement symbol lookup,
library search, compatible-version selection, or constructor policy. It keys
its immutable page cache by the exact identity already selected by packaging,
maps shared read-only pages and private writable pages, accounts references,
and reclaims them after the last mapping. Dependency traversal, relocation,
TLS layout, RELRO, symbol lookup, and initializer order belong exclusively to
the versioned user loader. This follows the proven BeOS/Haiku responsibility
split without importing Haiku's generic file-backed VM machinery: Astra's VFS
is a user service, so the kernel retains only the narrow streamed-image cache
needed to share immutable library pages.

`astra_vfs_library_source_open()` is the one runtime provider resolver. Both
the supervisor bootstrap and ordinary process loader use it. The process-level
helper is only a namespace-bound wrapper. A boundary test rejects direct
provider-index parsing or `.providers` path construction in either consumer,
and the Kit build rejects a shared-library product that has zero or multiple
providers. There is deliberately no directory-scan fallback: a missing or
corrupt index is a package/image error and fails with the exact requested
identity.

Ordinary Astra-native and POSIX executables therefore do not each embed a
private copy of loader machinery. Supervisor and Storage are explicitly static
because they must bootstrap the loader and system volume. The NDK also exposes
an explicit static link mode for developers who intentionally prefer
self-containment, without creating a second implementation or ABI.

There is no second library-open API and no typed export-table ABI. Optional
plugins and future runtime discovery must use this same identity resolver,
mapper, versioned-symbol contract, and process-owned lifetime; until that
single path is implemented, such loading is not exposed by the NDK.

## 4. Toolchain and packaging

The Astra GCC/binutils target, NDK, package builder, and loader share one
library-definition source of truth. A library build emits one `.library`, its
detached symbols, public headers, ABI report, and Kit/package metadata. ABI
checks reject removed or changed symbols within an ABI major. A major break is
installed beside the previous major.

Foundational C, math, C++, threading, and POSIX facilities are `.library`
implementations. Astra-native and POSIX programs use the same loader, startup
ABI, argument/environment representation, streams, exit path, diagnostics,
and library resolver. The POSIX library adds descriptor, path, signal,
threading, and `errno` semantics; it does not duplicate process startup or
linking.

Standards-defined facilities have one implementation in that shared runtime.
For example, the picolibc `regcomp`, `regexec`, `regerror`, and `regfree`
implementation supplies POSIX regular expressions to both native and POSIX
clients instead of being copied into each command. Similar-looking language
features are not merged when their contracts differ: Lua patterns remain part
of Lua, and Vim's editor-specific pattern language remains Vim's unless its
own conformance suite proves that a standards implementation is a drop-in
replacement.

Static linkage remains an explicit developer choice, not a fallback for an
unfinished loader. Static and dynamic builds use the same public headers and
implementation sources. Astra's production tree selects static mode for
Supervisor, the initial ROM image, and Storage, which mounts `SYS:` before the
loader and shared libraries are available. Ordinary commands, applications,
and services link dynamically.
