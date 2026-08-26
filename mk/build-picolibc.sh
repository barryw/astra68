#!/bin/sh
# Builds picolibc for Astra's m68030 target.
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
#   thread-local-storage  the runtime places thread state itself.
set -eu

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
SOURCE=${PICOLIBC_SOURCE:-$HERE/../third_party/picolibc}
BUILD=${PICOLIBC_BUILD:-$HOME/picolibc-build}
PREFIX=${PICOLIBC_PREFIX:-$HOME/picolibc-astra}
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
    -Dmultilib=false \
    -Dpicocrt=false \
    -Dthread-local-storage=false \
    -Dspecsdir=none \
    "$SOURCE"
ninja
ninja install
echo "picolibc installed to $PREFIX"
