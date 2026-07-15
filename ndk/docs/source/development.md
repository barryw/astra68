# Documentation Development

Public API documentation lives beside declarations in `include/astra`. Guide
pages and runnable examples live under `docs/source` and `examples`.

## Build products

```sh
make -C ndk docs-html
make -C ndk docs-pdf
make -C ndk docs
```

The outputs are:

- `ndk/build/docs/html/index.html`: searchable interactive documentation.
- `ndk/build/docs/astra68-ndk.pdf`: printable reference manual.

`make -C ndk sdk` builds the target library, runs host tests and sanitizers,
cross-compiles the example, and generates both documentation formats.

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

Every public declaration must explain its observable behavior. Functions also
document parameters, results, ownership transfer, blocking behavior, thread
safety, and the NDK version in which they appeared. Doxygen and Sphinx warnings
are build failures, including undocumented declarations and unresolved links.
