# Documentation Development

Public API documentation lives beside the declarations in `include/astra`.
Guide pages live under `docs/source` and examples under `examples`.

## Build products

```sh
make -C ndk docs-html
make -C ndk docs-pdf
make -C ndk docs
make -C ndk docs-check
```

The outputs are:

- `ndk/build/docs/html/index.html`: searchable interactive documentation.
- `ndk/build/docs/astra68-ndk.pdf`: printable reference manual.
- `ndk/build/docs/doxygen/xml`: the machine-readable API description, one
  XML compound per header, struct, and group. The distribution archive
  ships it under `docs/xml`.

`make -C ndk sdk` runs the header contract, builds every example, builds the
distribution archive with both documentation formats, links programs from the
extracted archive, and runs the tests of every OS library the archive ships.

The default docs build uses the pinned project container. A prepared native
environment with Doxygen, Sphinx, Breathe, MyST, Furo, and LaTeX can bypass
Docker:

```sh
ASTRA_NDK_DOCS_NATIVE=1 make -C ndk docs
```

Image construction defaults to Docker's host network because the FPGA build
host does not provide working bridge DNS. Set `ASTRA_NDK_DOCS_BUILD_NETWORK`
to another Docker build network when needed. The documentation generator itself
runs with networking disabled.

## Header contract

Every public declaration is documented, and every function documents each
parameter and its result; Doxygen and Sphinx warnings are build failures,
including undocumented declarations and unresolved links. The first sentence
of a comment is its brief description. `tests/test_documentation.py` requires
each documented header to carry an `@file` block and to appear on exactly one
API page. `make -C ndk header-contract` compiles every shipped header on its
own, as C11 and C++17, from the installed tree.
