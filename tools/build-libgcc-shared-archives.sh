#!/bin/sh
# Build Astra's shared-runtime inputs with GCC's own -DSHARED object rules.
#
# Usage: build-libgcc-shared-archives.sh LIBGCC_BUILD_DIR [CROSS_PREFIX]
#
# GCC's installed libgcc.a is PIC on Astra but intentionally marks every
# implementation hidden. Rewriting those ELF symbols after compilation would
# create a private, brittle ABI. GCC already has a supported shared-object
# recipe which compiles the same sources with default visibility; this script
# packages that output into the two acyclic archives consumed by Astra's own
# DSO linker and metadata format. The same upstream rules also own the PIC
# C++ begin/end files required by dynamic executables.

set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 LIBGCC_BUILD_DIR [CROSS_PREFIX]" >&2
    exit 2
fi

build_dir=$1
cross=${2:-m68k-astra-}
cc=${cross}gcc
ar=${cross}ar
ranlib=${cross}ranlib

test -f "$build_dir/Makefile" || {
    echo "$0: not a configured libgcc build directory: $build_dir" >&2
    exit 1
}
test "$("$cc" -dumpmachine)" = m68k-astra || {
    echo "$0: $cc does not target m68k-astra" >&2
    exit 1
}

libgcc=$($cc -print-libgcc-file-name)
install_dir=$(dirname "$libgcc")
work=$(mktemp -d "${TMPDIR:-/tmp}/astra-libgcc-shared.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

all=$work/libgcc-shared-all.a
builtins=$work/libgcc_builtins_shared.a
unwind=$work/libgcc_unwind_shared.a
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}

# A --disable-shared GCC does not put these in EXTRA_PARTS, but its normal
# crtstuff rules remain the authority for their contents. Rebuild explicitly
# so stale objects cannot retain flags from an earlier invocation.
rm -f "$build_dir/crtbeginS.o" "$build_dir/crtendS.o"
make -C "$build_dir" -j "$jobs" \
    CRTSTUFF_T_CFLAGS_S='-fPIC -ftls-model=initial-exec' \
    crtbeginS.o crtendS.o

# The custom link action asks GCC's makefiles for their exact shared object
# set, archives those objects, and creates the target marker expected by make.
rm -f "$build_dir/libgcc_s.astra-shared"
make -C "$build_dir" -j "$jobs" enable_shared=yes \
    SHLIB_EXT=.astra-shared \
    "SHLIB_LINK=$ar rcs $all @shlib_objs@ && touch libgcc_s.astra-shared" \
    libgcc_s.astra-shared

# @shlib_objs@ ends with the hidden static archive for GCC's normal fallback
# linker script. It must not be nested inside Astra's default-visible archive.
$ar d "$all" libgcc.a
$ranlib "$all"

cp "$all" "$builtins"
$ar d "$builtins" emutls_s.o unwind-dw2_s.o unwind-dw2-fde_s.o \
    unwind-sjlj_s.o unwind-c_s.o
$ranlib "$builtins"

for object in unwind-dw2_s.o unwind-dw2-fde_s.o unwind-sjlj_s.o \
        unwind-c_s.o; do
    test -f "$build_dir/$object" || {
        echo "$0: GCC did not build required object $object" >&2
        exit 1
    }
done
(
    cd "$build_dir"
    "$ar" rcs "$unwind" unwind-dw2_s.o unwind-dw2-fde_s.o \
        unwind-sjlj_s.o unwind-c_s.o
)
$ranlib "$unwind"

# Publish complete archives atomically. A concurrent Astra build sees either
# the previous pair or the new pair, never a partially written archive.
for archive in "$builtins" "$unwind"; do
    name=$(basename "$archive")
    temporary=$install_dir/.$name.tmp.$$
    cp "$archive" "$temporary"
    mv "$temporary" "$install_dir/$name"
done

for startup in crtbeginS.o crtendS.o; do
    temporary=$install_dir/.$startup.tmp.$$
    cp "$build_dir/$startup" "$temporary"
    mv "$temporary" "$install_dir/$startup"
done

echo "installed $install_dir/libgcc_builtins_shared.a"
echo "installed $install_dir/libgcc_unwind_shared.a"
echo "installed $install_dir/crtbeginS.o"
echo "installed $install_dir/crtendS.o"
