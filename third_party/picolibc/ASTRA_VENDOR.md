# picolibc, vendored

Upstream: <https://github.com/picolibc/picolibc>
Version: **1.8.12**, tag `1.8.12`, commit `2ae376c6cdf4fef90ca2388ecf7a07457fa63cff`
Vendored: 2026-08-19

## Why this library

Astra needed a C library and did not need to write one. picolibc is newlib and
avr-libc with the licence collection cleaned up — upstream removed every source
file that was not BSD-compatible, so the whole tree is BSD-3-Clause or MIT-like
and mixes with this repository without a question to answer. It is built for
freestanding targets: it assumes no operating system, and reaches the one it is
on through a handful of functions the integrator supplies.

It also has real m68k support — `libc/machine/m68k`, `picocrt/machine/m68k`, and
upstream cross files — which is not true of most of the alternatives. musl is
MIT and complete but every musl port is a Linux-syscall port; taking it would
have meant committing to a Linux personality in the kernel.

## What was removed, and what was added

* `test/` — 47 MB of upstream's test suite. The build only descends into it
  when `-Dtests=true`, and those tests execute m68k binaries, which no host
  here can do.
* `.github/`, `.git/` — CI and history, neither of which this repository is the
  right home for.
* `scripts/cross-m68k-astra.txt` — **added** for the Astra CPU/float and
  position-independent ABI.
  Upstream's `cross-m68k-linux-gnu.txt` targets `-march=68020` with the
  toolchain's default float ABI; Astra builds `-m68040 -msoft-float`. A libc
  built for a different float ABI links without a complaint and returns wrong
  answers. Picolibc explicitly disables Meson's automatic PIC setting on its
  top-level archives, so the cross contract supplies `-fPIC` directly; this is
  a file rather than a set of flags somebody remembers to pass.

* `libc/include/sys/_types.h` — removes the typedef cast from
  `__SSIZE_MAX__`. The value and C type conversion are unchanged, while the
  public `SSIZE_MAX` can now be used in a preprocessor `#if`, as portable
  applications including Vim require.
* `libc/include/sys/termios.h` — carries the conventional `ws_xpixel` and
  `ws_ypixel` members in `struct winsize`; terminal applications use the same
  four-field ioctl ABI as other POSIX systems.
* `libc/include/sys/resource.h` — defines `RLIM_NLIMITS` for the complete
  public resource-limit set and the conventional resource-usage counters.
  Astra reports the lifetime resident-memory high-water mark in KiB and leaves
  unsupported accounting counters at zero, so portable shells can consume the
  standard structure without target-specific substitutes.
* `libc/include/sys/unistd.h` — uses the modern `size_t` count for
  `setgroups()` and relies on `sys/features.h` for Astra's single advertised
  POSIX.1-2008 version instead of contradicting it with an unconditional
  POSIX.1-2024 definition.
* `libc/include/signal.h` — leaves signal-set operations as ABI calls on Astra
  instead of replacing them with unchecked inline shifts. The libc boundary
  validates signal numbers and pointers, and never exposes reserved bit zero
  as a signal.
* `libc/machine/m68k/setjmp.S` — emits the non-executable GNU stack note for
  every ELF target, including Astra, rather than only Linux ELF targets.
* `libc/machine/m68k/read_tp.S`, `set_tls.c` — use Astra's reserved A4 thread
  pointer instead of upstream's process-global `__tls` variable. This is both
  position independent and preserves distinct libc state for concurrent Astra
  threads.
* `libc/stdio/fgetwc.c`, `fputwc.c`, `fputws.c`, `getwchar.c`,
  `putwchar.c` — provide the GNU unlocked wide-I/O entry points declared by
  picolibc's public `wchar.h`. Upstream emits only part of that surface when
  stdio locking is enabled, leaving the other declared aliases unresolved.

A version bump is a re-vendor, not a merge. Retained source differences are
listed above so they cannot disappear into an installed sysroot.

## Building

    mk/build-picolibc.sh

which configures out of tree and installs to the compiler's reported Astra
sysroot by default. The
options that are not upstream defaults, and why:

| option | why |
|---|---|
| `-Dposix-console=true` | stdio reaches `read`/`write` on descriptors 0-2, which is what `sw/userspace/posix` implements over stream capabilities |
| `-Dpicocrt=false` | Astra has its own `crt0` and linker script |
| `-Dsemihost=false` | ARM debug-host I/O; there is no host to semihost to |
| `-Dtests=false` | they execute m68k binaries; see above |
| `-Dmb-capable=true` | enables UTF-8 conversion required by modern terminal programs |
| `-Dio-c99-formats=true` | keeps the complete C99 conversion family in formatted I/O |
| `-Dio-long-long=true` | implements the standard `ll` formatted-I/O length modifier |
| `-Dio-pos-args=true` | implements POSIX positional formatted-I/O arguments |
| `-Dio-long-double=true` | implements the standard `L` formatted-I/O length modifier |
| `-Dio-percent-b=true` | implements the C23 binary formatted-I/O conversion |
| `-Dprintf-percent-n=true` | implements the standard `%n` conversion rather than silently rejecting it |
| `-Dio-wchar=true` | supplies the wide-character stdio surface used by UTF-8 applications |
| `-Dstdio-locking=true` | makes stdio operations safe between Astra threads and supplies the locked/unlocked POSIX API pair |
| `-Dthread-local-storage=true` | libc state is per-thread; Astra initializes each program's PT_TLS image |
| `-Db_staticpic=true` | the installed archive is also the canonical input to `libc.library`; keeping a second libc implementation or source build would let static and dynamic behavior diverge |
| `-Dtls-model=initial-exec` | Astra resolves the complete dependency closure and TLS layout before entering a program; unlike local-exec, this model remains relocatable inside `libc.library` |
| `-Denable-malloc=false` | Astra's segregated-fit allocator is the sole allocator for native and POSIX code; picolibc must not add a selection-order-dependent second implementation |
| `-Dos-fallback=true` | split bare-metal fallback stubs into a separate archive; Astra deliberately does not link it because its own process, signal, and heap implementations are authoritative |
| `-Dmultilib=false` | one ABI, chosen in the cross file |

The installed public headers are the source of truth for `libc.library`'s
generated version map. Picolibc 1.8.12 does not annotate every public POSIX and
hardening declaration with `__picolibc_export`, so compiler visibility alone
is not a complete API description. The Astra build compiles position-
independent archive objects with normal visibility, intersects their symbols
with declarations from the installed headers, and hides everything else in
the DSO version script. This publishes the complete implemented API without
leaking implementation helpers. The build also adds `sw/include` to
picolibc's include search because its m68k TLS glue uses
the same `astra/tls.h` ABI constants as the kernel, loader, and runtime; those
constants are deliberately not copied into the vendor tree.

## The licence position

picolibc is BSD-3-Clause and MIT-family throughout; `COPYING.picolibc` carries
the full set. Both are compatible with this repository and neither is
copyleft — a binary linking it carries an attribution requirement and nothing
more. That was a selection criterion, not a discovery: GNU coreutils and
BusyBox were the obvious sources of Unix commands and both are GPL, which is
why neither is here.
