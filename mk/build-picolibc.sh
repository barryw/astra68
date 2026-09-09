#!/bin/sh
# Builds picolibc for Astra's m68040 target.
#
# The source is vendored at third_party/picolibc; see its ASTRA_VENDOR.md for
# what was removed and which source differences are retained. This builds out of
# tree, because the library is a build product and git holds source.
#
# Options that are not defaults, and why:
#   posix-console  stdio reaches read()/write() on fds 0-2, which is what
#                  sw/userspace/posix implements over stream capabilities.
#   picocrt=false  Astra has its own crt0 and linker script.
#   semihost       ARM debug-host I/O; there is no host to semihost to.
#   tests          they need to execute m68k binaries, which this host cannot.
#   mb-capable     POSIX terminal programs use UTF-8 multibyte conversion.
#   thread-local-storage  libc state is per-thread; Astra initializes PT_TLS.
set -eu

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
SOURCE=${PICOLIBC_SOURCE:-$HERE/../third_party/picolibc}
BUILD=${PICOLIBC_BUILD:-$HOME/picolibc-build}
DEFAULT_PREFIX=$(m68k-astra-gcc -print-sysroot)
PREFIX=${PICOLIBC_PREFIX:-$DEFAULT_PREFIX}
MESON=${MESON:-meson}

if [ ! -f "$SOURCE/meson.build" ]; then
    echo "no picolibc source at $SOURCE" >&2
    exit 1
fi

rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"
"$MESON" setup \
    --cross-file "$SOURCE/scripts/cross-m68k-astra.txt" \
    -Dprefix="$PREFIX" \
    -Dincludedir=include \
    -Dlibdir=lib \
    -Dposix-console=true \
    -Dsemihost=false \
    -Dtests=false \
    -Dmb-capable=true \
    -Dmultilib=false \
    -Dpicocrt=false \
    -Dthread-local-storage=true \
    -Dspecsdir=none \
    "$SOURCE"
ninja
ninja install
if ! grep -aq 'mcpu=68040' "$PREFIX/lib/libc.a" ||
        strings "$PREFIX/lib/libc.a" | grep 'mcpu=' | grep -qv 'mcpu=68040'; then
    echo "installed picolibc does not contain only MC68040 objects" >&2
    exit 1
fi
echo "picolibc installed to $PREFIX"
