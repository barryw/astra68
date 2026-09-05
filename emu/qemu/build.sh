#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROFILE=${1:-host}
SOURCE=$("$SCRIPT_DIR/prepare-source.sh")
SOURCE_ID=$(basename -- "$SOURCE")
BUILD_CONTRACT=$(cksum "$SCRIPT_DIR/build.sh" | awk '{print $1}')
WORK_ROOT=$(dirname -- "$SOURCE")
BUILD="$WORK_ROOT/build-$PROFILE-$BUILD_CONTRACT-${SOURCE_ID#source-}"

case "$PROFILE" in
    host|desktop|arty|arty-profile|de25|de25-profile)
        ;;
    *)
        echo "usage: $0 [host|desktop|arty|arty-profile|de25|de25-profile]" >&2
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
        arty|arty-profile|de25|de25-profile)
            DEBUG_INFO=--disable-debug-info
            PROFILE_OPTIONS=
            OPTIMIZATION='-O3 -fomit-frame-pointer'
            case "$PROFILE" in
                arty*)
                    CROSS_PREFIX=arm-linux-gnueabihf-
                    TARGET_CPU=arm
                    PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig
                    CPU_FLAGS='-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard'
                    ;;
                de25*)
                    CROSS_PREFIX=aarch64-linux-gnu-
                    TARGET_CPU=aarch64
                    DE25_SYSROOT=${DE25_SYSROOT:-${XDG_CACHE_HOME:-"$HOME/.cache"}/astra68/de25-jammy-arm64}
                    if ! grep -qx 'VERSION_ID="22.04"' "$DE25_SYSROOT/etc/os-release" 2>/dev/null; then
                        echo "DE25_SYSROOT is not an Ubuntu 22.04 AArch64 sysroot: $DE25_SYSROOT" >&2
                        exit 1
                    fi
                    COMPILER_INCLUDE=$("$CROSS_PREFIX"gcc -print-file-name=include)
                    PKG_CONFIG_SYSROOT_DIR=$DE25_SYSROOT
                    PKG_CONFIG_LIBDIR=$DE25_SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig:$DE25_SYSROOT/usr/share/pkgconfig
                    CPU_FLAGS='-mcpu=cortex-a55'
                    ;;
            esac
            case "$PROFILE" in
                *-profile)
                DEBUG_INFO=--enable-debug-info
                PROFILE_OPTIONS=--enable-plugins
                OPTIMIZATION='-O3 -fno-omit-frame-pointer'
                ;;
            esac
            EXTRA_CFLAGS="$CPU_FLAGS $OPTIMIZATION"
            EXTRA_LDFLAGS=
            case "$PROFILE" in
                de25*)
                    EXTRA_CFLAGS="--sysroot=$DE25_SYSROOT -nostdinc -I$COMPILER_INCLUDE -isystem $DE25_SYSROOT/usr/include/aarch64-linux-gnu -isystem $DE25_SYSROOT/usr/include $EXTRA_CFLAGS"
                    EXTRA_LDFLAGS="--sysroot=$DE25_SYSROOT"
                    ;;
            esac
            env \
                PKG_CONFIG_SYSROOT_DIR="${PKG_CONFIG_SYSROOT_DIR:-}" \
                PKG_CONFIG_LIBDIR="$PKG_CONFIG_LIBDIR" \
                PKG_CONFIG_PATH= \
                "$SOURCE/configure" \
                --target-list=m68k-softmmu \
                --cross-prefix="$CROSS_PREFIX" \
                --cpu="$TARGET_CPU" \
                --without-default-features \
                --enable-tcg \
                --enable-lto \
                "$DEBUG_INFO" \
                $PROFILE_OPTIONS \
                --enable-system \
                --enable-pixman \
                --enable-fdt \
                --disable-werror \
                --extra-cflags="$EXTRA_CFLAGS" \
                --extra-ldflags="$EXTRA_LDFLAGS"
            ;;
    esac
fi

if [ -n "${ASTRA_QEMU_JOBS:-}" ]; then
    ninja -C "$BUILD" -j "$ASTRA_QEMU_JOBS" qemu-system-m68k
else
    ninja -C "$BUILD" qemu-system-m68k
fi
printf '%s\n' "$BUILD/qemu-system-m68k"
