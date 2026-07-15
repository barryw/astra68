# Getting Started

Include the umbrella header for the complete public API:

```c
#include <astra/ndk.h>
```

Applications include headers from `include/astra` and link with `libastra.a`.
The default target is a freestanding Motorola 68030 build with software
floating point:

```sh
make -C ndk
```

Every function returning {c:type}`AstraResult` reports success as
{c:enumerator}`ASTRA_OK` and failures as a negative error code. Check those
results; public operations marked {c:macro}`ASTRA_NODISCARD` ask supported
compilers to diagnose ignored return values.

## Front-panel example

This example is also cross-compiled by `make -C ndk example`, so the published
sample cannot silently drift away from the headers.

```{literalinclude} ../../examples/front_panel.c
:language: c
:linenos:
```

## Font-service example

The font contract can be compiled and linked before the operating-system font
service is available. Applications discover that runtime state and fail cleanly
instead of assuming fonts exist:

```{literalinclude} ../../examples/font_layout.c
:language: c
:linenos:
```
