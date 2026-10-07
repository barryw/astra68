#!/bin/bash
# Generate the DE25 build sysroot (Ubuntu 22.04 jammy, arm64) that
# emu/qemu/build.sh and fpga/de25/linux/Makefile compile against:
#
#   tools/de25-sysroot.sh [DEST]   (default ~/.cache/astra68/de25-jammy-arm64)
#
# Installs exactly the packages in tools/de25-sysroot.packages, each fetched
# from the Ubuntu ports pool and checked against the SHA-256 pinned there, so
# the same list always yields the same files. Packages are unpacked, not
# configured: a sysroot needs headers, libraries and pkg-config files, not
# maintainer scripts. /bin, /sbin and /lib are merged-/usr symlinks, as on
# the board. DEST/astra-sysroot.manifest records what was installed.
#
# The board runs jammy-updates point releases of some of these libraries;
# they keep the sonames, so binaries linked here run there.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LIST=$HERE/de25-sysroot.packages
DEST=${1:-${XDG_CACHE_HOME:-$HOME/.cache}/astra68/de25-jammy-arm64}
MIRROR=${MIRROR:-http://ports.ubuntu.com/ubuntu-ports}

die() { echo "de25-sysroot: $*" >&2; exit 1; }

# Never unpack over an older tree: stale files would outlive the list.
[ ! -e "$DEST" ] || die "$DEST exists; move it aside first"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir -p "$DEST/usr/bin" "$DEST/usr/sbin" "$DEST/usr/lib"
ln -s usr/bin "$DEST/bin"
ln -s usr/sbin "$DEST/sbin"
ln -s usr/lib "$DEST/lib"
grep -Ev '^(#|$)' "$LIST" | while read -r name version sha256 path; do
    deb=$work/$(basename "$path")
    curl -sSfL --retry 3 -o "$deb" "$MIRROR/$path"
    echo "$sha256  $deb" | sha256sum -c --quiet - ||
        die "$name $version: SHA-256 mismatch"
    # --keep-directory-symlink keeps lib -> usr/lib when a package ships /lib.
    dpkg-deb --fsys-tarfile "$deb" |
        tar -x --keep-directory-symlink --no-same-owner -C "$DEST"
    rm "$deb"
done
grep -Ev '^(#|$)' "$LIST" | cut -d' ' -f1-3 > "$DEST/astra-sysroot.manifest"
grep -qx 'VERSION_ID="22.04"' "$DEST/etc/os-release" || die "no jammy os-release"
echo "de25-sysroot: $(wc -l < "$DEST/astra-sysroot.manifest") packages in $DEST"
