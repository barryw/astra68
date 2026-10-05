# Astra 68 Native Developer Kit

The Astra NDK is the supported C interface to Astra 68 hardware and operating
system services. Applications include public headers from `include/astra` and
link the versioned `.library` files shipped in `lib/m68040`; they do not include
raw chipset register structures. The loader caches immutable, versioned Kit
code and maps its read-only pages into each client process. Writable state and
service handles remain process-local. Static archives remain available as an
explicit developer choice for deliberately self-contained images. Astra uses
them for Supervisor and Storage because those images bootstrap the loader and
system volume.
Bounded message ports, absolute-deadline waits, atomic handle movement,
explicit shared areas, and bounded bulk rings are the native protected-process
communication plane.

This boundary provides source compatibility as the machine evolves. A hardware
register may move or be replaced without changing application source as long
as the corresponding NDK contract remains valid. The NDK API is implemented by
shared libraries (`system.library`, `graphics.library`, `interface.library`,
and the rest) that sit over the kernel and resident services; Astra keeps
those substantial implementations in shared Kits rather than copying them into
every application.

Shared hardware follows the process-owned handle and lease model in
[`docs/RESOURCE_MODEL.md`](../docs/RESOURCE_MODEL.md). Public mutating APIs
report contention and permission failures; they do not expose uncoordinated
global ownership registers.

The GCC-based target build also provides typed `ASTRA_AUTO_*` scope wrappers.
They release resources on normal scope exit, while the OS process handle table
remains responsible for cleanup after a crash or forced termination.

## Layout

```
include/astra/       public application API
docs/                Doxygen/Sphinx sources for the generated guide
make/                make fragments an application Makefile includes
tests/               distribution and header-contract tests
examples/            small application-facing examples
```

The NDK ships only public headers, documentation, examples, make fragments,
and distribution tests. Implementations live with the components that own
them under `sw/userspace`: `system.library` and `libastra.a` in
`sw/userspace/system`, UTF-8 in `sw/common/utf8.c`. The front-panel driver
lives in the boot ROM (`sw/boot`, header `sw/include/astra/front_panel.h`) and
is not part of the NDK.

`sw/include` remains the low-level firmware register interface used by ROM and
hardware diagnostics. It is not an application API.

## Build

The target library is freestanding. The cross compiler is selected by
`mk/m68k-cross.mk`, which prefers `m68k-astra-gcc`, falls back to
`m68k-linux-gnu-gcc`, then `m68k-elf-gcc`:

```sh
make -C ndk
make -C ndk test
make -C ndk dist-check
make -C ndk certify
```

`test` (aliased by `check`) runs the header contract: every shipped header
must compile alone as both C11 and C++17, plus `tests/test_documentation.py`.
Sanitizers (ASan/UBSan) and GCC's path-sensitive static analyzer run per
OS-library component under `sw/userspace` (each component's own `sanitize`
and `analyze` targets); `make -C ndk certify` (and `sdk`) invoke them for every
component through `os-library-tests`. There is no standalone `make -C ndk
sanitize` or `analyze` target.

Include `make/astra-native.mk` or `make/astra-posix.mk` from an application
Makefile. Their normal compile and link variables produce position-independent,
dynamically linked executables using `crt0-dynamic.o`; the archive's
`tools/check_dynamic_executable.py` validates the interpreter, exact direct
dependencies, immediate binding, RELRO, and absence of text relocations.
The deliberately named `ASTRA_STATIC_*` variables provide the explicit
self-contained link mode. Dynamic linking is the normal default because it
shares resident code and library updates; a developer may still choose static
linking when self-containment is the stronger requirement. Astra's production
tree uses that choice for Supervisor and Storage, which bootstrap the loader
and system volume.

Override `CROSS` or `CPU_FLAGS` for another compatible toolchain. Published
components include message ports in `astra/port.h`, shared areas in
`astra/area.h`, batched bulk IPC in `astra/bulk_ring.h`, font.library's UI and
monospace bitmap fonts in `astra/font_library.h`, and the complete Vega and
Astraea graphics contract in `astra/graphics.h`. Graphics applications work
through owned surfaces, palettes, sprite sets, raster programs, command
lists, and fences rather than raw MMIO; the NDK API is implemented by
`graphics.library` over the kernel and resident services, and a call that
needs a resource manager which has not landed yet returns
`ASTRA_ERROR_NOT_PRESENT`. `astra/graphics_kit.h` is the umbrella include for
graphics and fonts. Native programs call ordinary versioned ELF symbols; their
manifest and link metadata declare the required libraries, and the process
loader resolves the complete dependency closure under `LIBS:` before `main`
runs. The front-panel driver (`astra/front_panel.h`) is firmware, not an NDK
component; see the Layout section above.

`astra/filesystem_kit.h` publishes the corresponding Filesystem Kit identity;
its direct API provides high-level file/directory operations and the existing
low-level VFS primitives without exposing storage-service internals.

`astra/interface_kit.h` publishes the Interface Kit identity and direct API.
Its controls, responsive layout, TextSurface, typed clipboard, and
undo manager are documented in the generated Interface Kit guide. Checked
examples cover the common control lifecycle, text copy/paste, and reversible
document operations.

## Documentation

Public API documentation is written with the declarations in `include/astra`.
Doxygen extracts those contracts, and Sphinx combines them with the NDK guides
and checked examples to produce both documentation formats:

```sh
make -C ndk docs-html
make -C ndk docs-pdf
make -C ndk docs
```

The interactive manual is generated at `ndk/build/docs/html/index.html`; the
printable manual is generated at `ndk/build/docs/astra68-ndk.pdf`. The default
documentation build uses a pinned container so developers and CI use the same
Doxygen, Sphinx, Breathe, MyST, Furo, and LaTeX toolchain.

Run `make -C ndk sdk` for the complete SDK gate: target library, host tests,
sanitizers, static analysis, compiled examples, OS-library contract tests, HTML,
PDF, and a relocatable NDK archive. The archive is written as
`ndk/build/dist/astra68-ndk-<astra-os-version>.tar.xz`; its NDK version is the
Astra OS version by definition. The archive also ships `include/lua` (Lua
headers), `include/ncursesw` (terminfo headers), `include/SDL2`,
`include/posix`, and `docs/xml` (the Doxygen XML, for machine-readable API
access) alongside `include/astra` and the HTML/PDF guides. `make -C ndk
dist-check` extracts that archive and, using only its contents and the
installed `m68k-astra` compiler, dynamically links native C, POSIX C, and
POSIX C++ programs, their static-linked variants, and an SDL program. Each
smoke program also passes the packaged dynamic-executable contract checker.

Documentation warnings are errors, including undocumented public structures,
members, callbacks, declarations, parameters, return values, and unresolved
links. Every shipped public header must explicitly participate in the gate.
`make -C ndk docs-check`
provides the focused documentation completeness gate; `make -C ndk certify`
adds every existing host, sanitizer, analyzer, shared-library ABI, archive, and
link contract owned by the NDK and its OS-library implementations.

## Compatibility policy

- Public names and behavior are versioned NDK contracts.
- The NDK and Astra OS always use the same SemVer from `astra/version.h`;
  neither may be versioned independently.
- Libraries follow semantic versioning: major breaks compatibility, minor adds
  append-only features, and patch fixes behavior without changing the ABI.
- Public structures use fixed-width fields and reserve room before incompatible
  growth is considered.
- Hardware addresses, register offsets, and volatile register structures are
  private implementation details.
- Shared resources use opaque handles, explicit acquisition, and deterministic
  cleanup rather than global mutable state.
- New hardware support lands with its public API, implementation, tests, and
  documentation in the same change.
- Libraries are versioned by soname major and symbol versions (for example
  `system.library.3` ABI 3.0, `runtime.library.1` ABI 1.10); a major bump
  marks an incompatible change. A minor or patch bump is binary compatible.
- Userspace is hard float: the MC68040 FPU, not a soft-float emulation
  library.
