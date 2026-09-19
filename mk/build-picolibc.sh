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
#   complete-io    C and POSIX formatting, wide I/O and stdio locking are
#                  contracts, not size-saving options on Astra.
#   thread-local-storage  libc state is per-thread; Astra initializes PT_TLS.
#   b_staticpic    libc.a is also the input used to produce libc.library; one
#                  implementation must serve static recovery images and normal
#                  dynamically linked programs.
#   tls-model      local-exec cannot be relocated as part of a shared library;
#                  initial-exec keeps direct MC68040 TLS access while allowing
#                  the loader to assign libc's module offset eagerly.
#   enable-malloc  Astra's allocator is the one C allocator; building a second
#                  implementation into the canonical archive would make
#                  static and dynamic selection order-dependent.
#   os-fallback    Keep picolibc's bare-metal stubs in a separate archive.
#                  Astra does not link that archive because it supplies the
#                  operating-system calls itself.
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
    -Dio-c99-formats=true \
    -Dio-long-long=true \
    -Dio-pos-args=true \
    -Dio-long-double=true \
    -Dio-percent-b=true \
    -Dprintf-percent-n=true \
    -Dio-wchar=true \
    -Dstdio-locking=true \
    -Dmultilib=false \
    -Dpicocrt=false \
    -Dthread-local-storage=true \
    -Db_staticpic=true \
    -Dtls-model=initial-exec \
    -Denable-malloc=false \
    -Dos-fallback=true \
    -Dc_args="-I$HERE/../sw/include" \
    -Dspecsdir=none \
    "$SOURCE"
ninja
ninja install
if ! grep -aq 'mcpu=68040' "$PREFIX/lib/libc.a" ||
        strings "$PREFIX/lib/libc.a" | grep 'mcpu=' | grep -qv 'mcpu=68040'; then
    echo "installed picolibc does not contain only MC68040 objects" >&2
    exit 1
fi

# libc.library selects its ABI from installed public headers and can only
# publish archive definitions with normal ELF visibility. Verify representative
# C and POSIX symbols here; the complete header-to-DSO check runs in the POSIX
# library build.
verify_default_symbol()
{
    symbol=$1
    if ! m68k-astra-readelf -sW "$PREFIX/lib/libc.a" | awk -v symbol="$symbol" '
        $5 == "GLOBAL" && $6 == "DEFAULT" && $7 != "UND" && $8 == symbol {
            found = 1
        }
        END { exit !found }
    '; then
        echo "installed picolibc symbol is not publicly visible: $symbol" >&2
        exit 1
    fi
}

verify_default_symbol __xpg_strerror_r
verify_default_symbol nl_langinfo
verify_default_symbol nl_langinfo_l
verify_default_symbol ferror_unlocked
verify_default_symbol fgetwc_unlocked
verify_default_symbol fputws_unlocked
verify_default_symbol fputwc_unlocked
verify_default_symbol getwchar_unlocked
verify_default_symbol putwchar_unlocked
echo "picolibc installed to $PREFIX"
