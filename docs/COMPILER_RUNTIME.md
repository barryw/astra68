# Compiler runtime ownership

Astra dynamically linked code uses one implementation of every compiler
helper. GCC's MC68040 arithmetic/conversion builtins live in
`compiler.library.1`; DWARF exception handling and frame registration live in
`unwind.library.1`.

The split is a dependency boundary, not two competing runtimes:

```text
compiler.library.1                 (no dependencies)
        |
        +--> runtime.library.1
        +--> libc.library.1
                  |
                  +--> unwind.library.1
                            |
                            +--> cxx.library.1
```

The unwinder needs allocation, memory, string, and pthread operations from
libc. Keeping it above libc avoids a dependency cycle. Astra uses native ELF
TLS, so GCC's `emutls` fallback is intentionally absent.

GCC's installed static `libgcc.a` hides its definitions and cannot be used as
the source of a shared ABI. `tools/build-libgcc-shared-archives.sh` invokes
GCC's own `-DSHARED` build rules to install default-visible PIC input archives
beside `libgcc.a`. Astra then links those inputs with its standard `.library`
layout, identity record, symbol versions, immediate binding, and RELRO.

Build the inputs from the matching configured target-libgcc directory:

```sh
tools/build-libgcc-shared-archives.sh \
  /path/to/gcc-build/m68k-astra/libgcc
```

The toolchain content stamp includes both generated archives. Replacing either
one therefore invalidates every affected Astra build product.

A toolchain configured with `--disable-shared` also produces a non-PIC
`libstdc++.a`; it cannot safely be embedded in an Astra shared library.
`tools/build-libstdcxx-shared-archive.sh` reproduces the configured upstream
libstdc++ build in an isolated sibling directory with PIC enabled and
atomically installs `libstdc++_shared.a`. `cxx.library` consumes that archive
and depends on `libc.library`, `runtime.library`, `compiler.library`, and
`unwind.library`; it does not contain private copies of their implementations.

Build that input from the matching configured target-libstdc++ directory:

```sh
tools/build-libstdcxx-shared-archive.sh \
  /path/to/gcc-build/m68k-astra/libstdc++-v3
```

The toolchain content stamp covers this archive as well, so replacing it
invalidates every affected Astra build product.

`tools/check_shared_symbol_ownership.py` is the regression gate. It compares a
library's complete symbol table—including definitions localized by a version
script—with the public definitions of its dependencies. A duplicate owner is a
build failure, even when the duplicate would not be externally visible.
