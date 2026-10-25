#!/bin/bash
# Build the DE25 storage modules (astra-storage.config) from the exact-source
# kernel tree, and optionally install them on the board. Run on beast under
# the build lock:
#
#   ~/astra-mg/locked fpga/de25/linux/kernel/build-storage-modules.sh \
#       [--install root@192.168.1.52]
#
# The modules go to /lib/modules/$RELEASE/updates/astra-storage/ with a
# MANIFEST of their SHA-256s. Reinstalling unchanged files is a no-op. To
# reverse: rm -r that directory and run depmod -a $RELEASE. Nothing is loaded;
# the modules autoload by fs-* alias on mount and by modalias on attach.
set -euo pipefail

KERNEL_SOURCE=${KERNEL_SOURCE:-$HOME/astra-linux-6.12.11}
KERNEL_OUTPUT=${KERNEL_OUTPUT:-$HOME/astra-linux-6.12.11-build}
RELEASE=6.12.11-gd7d192a9ddd9
HERE=$(cd "$(dirname "$0")" && pwd)
FRAGMENT=$HERE/astra-storage.config
BASELINE=$HERE/de25-$RELEASE.config
OUT=${OUT:-$KERNEL_OUTPUT/astra-storage}
REMOTE_DIR=/lib/modules/$RELEASE/updates/astra-storage
MODULES="fs/isofs/isofs.ko fs/udf/udf.ko fs/exfat/exfat.ko fs/ntfs3/ntfs3.ko fs/nls/nls_utf8.ko
drivers/usb/storage/uas.ko"

die() { echo "build-storage-modules: $*" >&2; exit 1; }
kmake() {
    make -C "$KERNEL_SOURCE" O="$KERNEL_OUTPUT" ARCH=arm64 \
        CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" "$@"
}

install_to=
case "${1:-}" in
    "") ;;
    --install) install_to=${2:?--install needs user@host} ;;
    *) die "usage: $0 [--install user@host]" ;;
esac

[ "$(cat "$KERNEL_OUTPUT/include/config/kernel.release")" = "$RELEASE" ] ||
    die "$KERNEL_OUTPUT is not the $RELEASE output tree"

"$KERNEL_SOURCE/scripts/kconfig/merge_config.sh" -m -O "$KERNEL_OUTPUT" \
    "$KERNEL_OUTPUT/.config" "$FRAGMENT" >/dev/null
kmake olddefconfig >/dev/null

# Every fragment line must survive olddefconfig: an unmet dependency drops it.
grep -E '^CONFIG_' "$FRAGMENT" | while IFS= read -r line; do
    grep -qxF "$line" "$KERNEL_OUTPUT/.config" || die "olddefconfig dropped $line"
done

# The modules must not see an Image the board does not run. Compared with the
# board's config, only the fragment, the DesignWare DMA modules the board
# already loads (fpga/de25/linux/README.md) and symbols the build toolchain
# detects for itself may differ.
set_symbols() { grep -E '^CONFIG_[A-Za-z0-9_]+=' "$1" | sort; }
drift=$(diff <(set_symbols "$BASELINE") <(set_symbols "$KERNEL_OUTPUT/.config") |
    sed -n 's/^[<>] \(CONFIG_[A-Za-z0-9_]*\)=.*/\1/p' | sort -u |
    grep -vE '^CONFIG_(CC_|GCC_|RUSTC_|AS_VERSION$|LD_VERSION$|ARCH_SUPPORTS_SHADOW_CALL_STACK$|DW_AXI_DMAC$|DMATEST$)' |
    grep -vxF -f <(grep -oE '^CONFIG_[A-Za-z0-9_]+' "$FRAGMENT") || true)
[ -z "$drift" ] || die "config differs from the board beyond the fragment: $drift"

# shellcheck disable=SC2086 # MODULES is a word list
kmake $MODULES
expected="$RELEASE SMP preempt mod_unload aarch64"
mkdir -p "$OUT"
for module in $MODULES; do
    name=$(basename "$module")
    vermagic=$(modinfo -F vermagic "$KERNEL_OUTPUT/$module" | sed 's/ *$//')
    [ "$vermagic" = "$expected" ] || die "$name vermagic '$vermagic' != '$expected'"
    aarch64-linux-gnu-strip --strip-debug -o "$OUT/$name" "$KERNEL_OUTPUT/$module"
done
(cd "$OUT" && sha256sum ./*.ko | sed 's# \./# #' > MANIFEST)
cat "$OUT/MANIFEST"

[ -n "$install_to" ] || exit 0

remote_release=$(ssh "$install_to" uname -r)
[ "$remote_release" = "$RELEASE" ] || die "$install_to runs $remote_release, not $RELEASE"
# The Image's own modules carry the vermagic the running kernel accepts.
remote_vermagic=$(ssh "$install_to" modinfo -F vermagic fuse | sed 's/ *$//')
[ "$remote_vermagic" = "$expected" ] ||
    die "$install_to vermagic '$remote_vermagic' != '$expected'"

ssh "$install_to" "mkdir -p $REMOTE_DIR"
while read -r sum name; do
    if [ "$(ssh -n "$install_to" "sha256sum $REMOTE_DIR/$name 2>/dev/null" | cut -d' ' -f1)" = "$sum" ]; then
        echo "unchanged $REMOTE_DIR/$name"
        continue
    fi
    scp -q "$OUT/$name" "$install_to:$REMOTE_DIR/$name.new"
    ssh -n "$install_to" "mv $REMOTE_DIR/$name.new $REMOTE_DIR/$name"
    echo "installed $REMOTE_DIR/$name"
done < "$OUT/MANIFEST"
scp -q "$OUT/MANIFEST" "$install_to:$REMOTE_DIR/MANIFEST"
ssh "$install_to" "cd $REMOTE_DIR && sha256sum -c --quiet MANIFEST && depmod -a $RELEASE"
echo "depmod -a $RELEASE done on $install_to"
