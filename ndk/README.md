# Astra 68 Native Developer Kit

The Astra NDK is the supported C interface to Astra 68 hardware and operating
system services. Applications include public headers from `include/astra` and
link `libastra.a`; they do not include raw chipset register structures.
Bounded message ports, absolute-deadline waits, atomic handle movement,
explicit shared areas, and bounded bulk rings are the native protected-process
communication plane.

This boundary provides source compatibility as the machine evolves. A hardware
register may move or be replaced without changing application source as long
as the corresponding NDK contract remains valid. Bare-metal builds currently
use the direct-MMIO backend. The operating system can later provide the same
API through libraries or system calls.

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
sanitizers, static analysis, compiled example, HTML, and PDF. Documentation
warnings are errors, including undocumented declarations and unresolved links.

## Compatibility policy

- Public names and behavior are versioned NDK contracts.
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
