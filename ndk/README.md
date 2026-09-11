# Astra 68 Native Developer Kit

The Astra NDK is the supported C interface to Astra 68 hardware and operating
system services. During bring-up applications include public headers from
`include/astra` and link `libastra.a`; they do not include raw chipset register
structures. The permanent ABI keeps the same headers behind a small import
veneer while the loader caches immutable, versioned Kit code and maps its
read-only pages into each client process. Writable state and service handles
remain process-local.
Bounded message ports, absolute-deadline waits, atomic handle movement,
explicit shared areas, and bounded bulk rings are the native protected-process
communication plane.

This boundary provides source compatibility as the machine evolves. A hardware
register may move or be replaced without changing application source as long
as the corresponding NDK contract remains valid. Bare-metal builds currently
use the direct-MMIO backend. Protected Astra keeps substantial implementations
in resident services and shared Kits rather than copying them into every
application.

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
src/                 library implementations
src/internal/        private hardware ABI and MMIO access
tests/               host-side API/backend tests
examples/            small application-facing examples
```

`sw/include` remains the low-level firmware register interface used by ROM and
hardware diagnostics. It is not an application API.

## Build

The target library is freestanding and defaults to the m68k Linux cross tools:

```sh
make -C ndk
make -C ndk test
make -C ndk sanitize
make -C ndk analyze
make -C ndk example
```

`sanitize` runs the host API tests under ASan/UBSan. `analyze` runs GCC's
path-sensitive static analyzer and is intended for the Linux build hosts.

Override `CROSS` or `CPU_FLAGS` for another compatible toolchain. Published
components include message ports in `astra/port.h`, shared areas in
`astra/area.h`, batched bulk IPC in `astra/bulk_ring.h`, managed front-panel
access in `astra/front_panel.h`, the
font/text-layout service contract in `astra/font.h`, and the complete Vega and
Astraea graphics contract in `astra/graphics.h`. Graphics applications work
through owned surfaces, palettes, tile/sprite sets, raster programs, command
lists, and fences rather than raw MMIO. The direct backend currently provides
the contract and validation boundary; services which require the operating
system return `ASTRA_ERR_UNAVAILABLE` until their resource manager lands.
`astra/graphics_kit.h` is the umbrella include for graphics and fonts and
publishes the logical names and minimum ABI versions accepted by
`OpenLibrary()`. The loader, not the application, resolves their versioned
files under `LIBS:`.

`astra/filesystem_kit.h` publishes the corresponding Filesystem Kit identity;
its typed API provides high-level file/directory operations and the existing
low-level VFS primitives without exposing storage-service internals.

`astra/interface_kit.h` publishes the Interface Kit identity and append-only
ABI table. Its controls, responsive layout, TextSurface, typed clipboard, and
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
Astra OS version by definition. `make -C ndk dist-check` also extracts that
archive and links native C, POSIX C, and POSIX C++ programs using only its
contents and the installed `m68k-astra` compiler.

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
- Binary compatibility is not promised before the operating-system library ABI
  is defined. Source compatibility across a rebuild is the current guarantee.
