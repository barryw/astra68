#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROFILE=${1:-host}
SOURCE=$("$SCRIPT_DIR/prepare-source.sh")
SOURCE_ID=$(basename -- "$SOURCE")
WORK_ROOT=$(dirname -- "$SOURCE")
BUILD="$WORK_ROOT/build-$PROFILE-${SOURCE_ID#source-}"

case "$PROFILE" in
    host|desktop|arty)
        ;;
    *)
        echo "usage: $0 [host|desktop|arty]" >&2
        exit 2
        ;;
esac

mkdir -p "$BUILD"
if [ ! -f "$BUILD/build.ninja" ]; then
    cd "$BUILD"
    case "$PROFILE" in
        host)
            "$SOURCE/configure" \
                --target-list=m68k-softmmu \
                --without-default-features \
                --enable-tcg \
                --enable-system \
                --enable-pixman \
                --enable-fdt \
                --disable-werror
            ;;
        desktop)
            if [ "$(uname -s)" = Darwin ]; then
                "$SOURCE/configure" \
                    --target-list=m68k-softmmu \
                    --without-default-features \
                    --enable-tcg \
                    --enable-system \
                    --enable-pixman \
                    --enable-fdt \
                    --enable-cocoa \
                    --disable-werror
            else
                "$SOURCE/configure" \
                    --target-list=m68k-softmmu \
                    --without-default-features \
                    --enable-tcg \
                    --enable-system \
                    --enable-pixman \
                    --enable-fdt \
                    --enable-sdl \
                    --disable-werror
            fi
            ;;
        arty)
            env \
                PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig \
                PKG_CONFIG_PATH= \
                "$SOURCE/configure" \
                --target-list=m68k-softmmu \
                --cross-prefix=arm-linux-gnueabihf- \
                --cpu=arm \
                --without-default-features \
                --enable-tcg \
                --enable-system \
                --enable-pixman \
                --enable-fdt \
                --disable-werror \
                --extra-cflags='-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -O3 -fomit-frame-pointer'
            ;;
    esac
fi

if [ -n "${ASTRA_QEMU_JOBS:-}" ]; then
    ninja -C "$BUILD" -j "$ASTRA_QEMU_JOBS" qemu-system-m68k
else
    ninja -C "$BUILD" qemu-system-m68k
fi
printf '%s\n' "$BUILD/qemu-system-m68k"
