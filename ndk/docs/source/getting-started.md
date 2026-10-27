# Getting Started

Include the umbrella header for the complete public API, or a narrower Kit
header such as `astra/graphics_kit.h`:

```c
#include <astra/ndk.h>
```

## The target

Programs run on a Motorola MC68040 with its FPU: the `m68k-astra` compiler
generates hard-float code by default and tags every object with its float
ABI, so a soft-float object cannot be linked in by accident. A program is a
position-independent executable that the system loader binds to versioned
shared libraries (`runtime.library.1`, `system.library.4`,
`graphics.library.3`, and so on) before it starts.

## Building a program

Include `make/astra-native.mk` (native programs) or `make/astra-posix.mk`
(programs written against the C library) from the archive and use its
variables. A native program declares its identity with `ASTRA_PROGRAM` and
starts at `astra_main`. The terminal that launches it hands it `STDOUT`:

```{literalinclude} ../../examples/hello.c
:language: c
```

```make
include $(ASTRA_NDK_ROOT)/make/astra-native.mk

hello: hello.c
	$(ASTRA_CC) $(ASTRA_CPPFLAGS) $(ASTRA_CFLAGS) $(ASTRA_LDFLAGS) \
		-o $@ $(ASTRA_CRT0) $< -Wl,-l:streams.library.1 \
		$(ASTRA_NATIVE_LIBS)
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(ASTRA_READELF) \
		--needed runtime.library.1 --needed compiler.library.2 $@
```

A POSIX program keeps its standard `main` and links
`src/astra-posix-program.c`, compiled with the `ASTRA_POSIX_PROGRAM_*`
identity macros, together with `$(ASTRA_POSIX_LIBS)`.
`$(ASTRA_STATIC_LDFLAGS)` and the `ASTRA_STATIC_*` variables build a
self-contained image instead; the system uses that only where the loader or
the system volume may not be available yet.

For new C/C++ ports, use CMake and Ninja with the extracted NDK toolchain:

```sh
cmake -S . -B build-astra -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/ndk/cmake/m68k-astra-toolchain.cmake
cmake --build build-astra
```

```cmake
cmake_minimum_required(VERSION 3.21)
project(hello C)
find_package(Astra CONFIG REQUIRED)
add_executable(hello hello.c)
target_link_libraries(hello PRIVATE Astra::Native)
```

Native programs provide `astra_main` and identity through `ASTRA_PROGRAM`.
POSIX programs retain `main` and call
`astra_posix_program(hello NAME hello VERSION 1.0.0 AUTHOR "Author"
COPYRIGHT "Copyright holder")`. Add `CXX` for C++ programs. SDL ports call
`find_package(SDL2 CONFIG REQUIRED)` and link `SDL2::SDL2`; SDL2 addon
packages expose matching imported targets. `tests/cmake-smoke/CMakeLists.txt`
is the installed archive's checked example.

## Results

Every function returning {c:type}`AstraResult` reports success as
{c:enumerator}`ASTRA_OK` and failures as a negative error code. Check those
results; public operations marked {c:macro}`ASTRA_NODISCARD` ask supported
compilers to diagnose ignored return values. Functions that return a
`uint32_t` status return the kernel's `ASTRA_SYSCALL_*` or a service's
status codes, as each declaration states.

## An example

The examples in `examples/` are complete programs. `examples/Makefile`
builds each one from the installed kit, and every release runs them all on
the machine and checks what they print, so none can drift from the API.
A one-shot timer re-armed for absolute deadlines:

```{literalinclude} ../../examples/timer.c
:language: c
:linenos:
```

A message sent through a port the program owns:

```{literalinclude} ../../examples/port_message.c
:language: c
:linenos:
```
