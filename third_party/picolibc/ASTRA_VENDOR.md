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
* `scripts/cross-m68k-astra.txt` — **added** for the Astra CPU/float ABI.
  Upstream's `cross-m68k-linux-gnu.txt` targets `-march=68020` with the
  toolchain's default float ABI; Astra builds `-m68030 -msoft-float`. A libc
  built for a different float ABI links without a complaint and returns wrong
  answers, so this is a file rather than a flag somebody remembers to pass.

* `libc/include/sys/_types.h` — removes the typedef cast from
  `__SSIZE_MAX__`. The value and C type conversion are unchanged, while the
  public `SSIZE_MAX` can now be used in a preprocessor `#if`, as portable
  applications including Vim require.
* `libc/include/sys/termios.h` — carries the conventional `ws_xpixel` and
  `ws_ypixel` members in `struct winsize`; terminal applications use the same
  four-field ioctl ABI as other POSIX systems.

A version bump is a re-vendor, not a merge. Retained source differences are
listed above so they cannot disappear into an installed sysroot.

## Building

    mk/build-picolibc.sh

which configures out of tree and installs to `~/picolibc-astra` by default. The
options that are not upstream defaults, and why:

| option | why |
|---|---|
| `-Dposix-console=true` | stdio reaches `read`/`write` on descriptors 0-2, which is what `sw/userspace/posix` implements over stream capabilities |
| `-Dpicocrt=false` | Astra has its own `crt0` and linker script |
| `-Dsemihost=false` | ARM debug-host I/O; there is no host to semihost to |
| `-Dtests=false` | they execute m68k binaries; see above |
| `-Dthread-local-storage=false` | the Astra runtime places thread state itself |
| `-Dmultilib=false` | one ABI, chosen in the cross file |

## The licence position

picolibc is BSD-3-Clause and MIT-family throughout; `COPYING.picolibc` carries
the full set. Both are compatible with this repository and neither is
copyleft — a binary linking it carries an attribution requirement and nothing
more. That was a selection criterion, not a discovery: GNU coreutils and
BusyBox were the obvious sources of Unix commands and both are GPL, which is
why neither is here.
