#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
target=${1:-}
case "$target" in
    host|de25) ;;
    *) echo "usage: $0 host|de25" >&2; exit 2 ;;
esac

cd "$root"
printf '%s\n' \
    '64926eebf90092dca21b14259a5301b7b98e7b1943e8a201c7d726084809b527  third_party/storage-format/dosfstools-4.2.tar.gz' \
    '4b5052cba6ff9f6dcb1c63eeeac328620472c5b0b3caf3d38e4ada50e75469d3  third_party/storage-format/exfatprogs-1.4.3.tar.gz' | sha256sum -c -

mkdir -p build/storage-format
scratch=$(mktemp -d "build/storage-format/.${target}.XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT
tar -xzf third_party/storage-format/dosfstools-4.2.tar.gz -C "$scratch"
tar -xzf third_party/storage-format/exfatprogs-1.4.3.tar.gz -C "$scratch"
patch -d "$scratch/dosfstools-4.2" -p1 < third_party/storage-format/patches/dosfstools-astra-fd.patch
patch -d "$scratch/exfatprogs-1.4.3" -p1 < third_party/storage-format/patches/exfatprogs-astra-fd.patch

configure_args=()
if [[ "$target" == de25 ]]; then
    sysroot=${DE25_SYSROOT:-$HOME/.cache/astra68/de25-jammy-arm64}
    [[ -d "$sysroot/usr/include" ]] || { echo "DE25 sysroot missing: $sysroot" >&2; exit 1; }
    export CC="aarch64-linux-gnu-gcc --sysroot=$sysroot"
    compiler_include=$(aarch64-linux-gnu-gcc -print-file-name=include)
    export CPPFLAGS="-nostdinc -I$compiler_include -isystem $sysroot/usr/include/aarch64-linux-gnu -isystem $sysroot/usr/include"
    export PKG_CONFIG_SYSROOT_DIR="$sysroot"
    export PKG_CONFIG_LIBDIR="$sysroot/usr/lib/aarch64-linux-gnu/pkgconfig:$sysroot/usr/share/pkgconfig"
    configure_args=(--host=aarch64-linux-gnu)
fi

(
    cd "$scratch/dosfstools-4.2"
    ./configure "${configure_args[@]}" --disable-compat-symlinks > configure.log
    make -j4 -C src mkfs.fat fsck.fat > build.log
)
(
    cd "$scratch/exfatprogs-1.4.3"
    ./configure "${configure_args[@]}" > configure.log
    make -j4 -C lib > lib-build.log
    make -j4 -C mkfs > mkfs-build.log
    make -j4 -C fsck > fsck-build.log
)

out="build/storage-format/$target"
mkdir -p "$out"
install -m 0755 "$scratch/dosfstools-4.2/src/mkfs.fat" "$out/astra-mkfs.fat"
install -m 0755 "$scratch/dosfstools-4.2/src/fsck.fat" "$out/astra-fsck.fat"
install -m 0755 "$scratch/exfatprogs-1.4.3/mkfs/mkfs.exfat" "$out/astra-mkfs.exfat"
install -m 0755 "$scratch/exfatprogs-1.4.3/fsck/fsck.exfat" "$out/astra-fsck.exfat"
sha256sum "$out"/astra-*.fat "$out"/astra-*.exfat
