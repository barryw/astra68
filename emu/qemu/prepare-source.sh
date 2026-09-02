#!/bin/sh
set -eu

QEMU_VERSION=9.2.4
QEMU_ARCHIVE="qemu-${QEMU_VERSION}.tar.xz"
QEMU_ARCHIVE_SHA256=f3cc1c4eabfdb288218ac3e33763dbe9e276d8bc890b867a2335d58de2ddd39a
QEMU_URL="https://download.qemu.org/${QEMU_ARCHIVE}"

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
OVERLAY="$SCRIPT_DIR/qemu-9.2"
PUBLIC_INPUT="$REPOSITORY/sw/include/astra/input.h"
PUBLIC_DISPLAY="$REPOSITORY/sw/include/astra/display.h"
PUBLIC_DISPLAY_MAILBOX="$REPOSITORY/sw/include/astra/display_mailbox.h"
PUBLIC_RENDER_BATCH="$REPOSITORY/sw/include/astra/render_batch.h"
PUBLIC_RENDER_PROTOCOL="$REPOSITORY/fpga/arty/linux/astra_render_protocol.h"
PUBLIC_SYSCALL="$REPOSITORY/sw/include/astra/syscall.h"
PUBLIC_MESSAGE_ABI="$REPOSITORY/sw/include/astra/message_abi.h"
PUBLIC_LIMITS="$REPOSITORY/sw/include/astra/limits.h"
PUBLIC_BLOCK="$REPOSITORY/sw/include/astra/block.h"
PUBLIC_NETWORK="$REPOSITORY/sw/include/astra/network.h"
PUBLIC_HOST="$REPOSITORY/sw/include/astra/host.h"
PUBLIC_STATUS="$REPOSITORY/sw/include/astra/status.h"
PUBLIC_VFS_SERVICE="$REPOSITORY/sw/include/astra/vfs_service.h"

sha256_file()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

sha256_stream()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | awk '{print $1}'
    else
        shasum -a 256 | awk '{print $1}'
    fi
}

overlay_identity()
{
    (
        cd "$OVERLAY"
        find . -type f -print | LC_ALL=C sort | while IFS= read -r file; do
            printf '%s  %s\n' "$(sha256_file "$file")" "$file"
        done
        printf '%s  %s\n' "$(sha256_file "$SCRIPT_DIR/prepare-source.sh")" \
            "emu/qemu/prepare-source.sh"
        printf '%s  %s\n' "$(sha256_file "$SCRIPT_DIR/build.sh")" \
            "emu/qemu/build.sh"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_INPUT")" \
            "sw/include/astra/input.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_DISPLAY")" \
            "sw/include/astra/display.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_DISPLAY_MAILBOX")" \
            "sw/include/astra/display_mailbox.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_RENDER_BATCH")" \
            "sw/include/astra/render_batch.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_RENDER_PROTOCOL")" \
            "fpga/arty/linux/astra_render_protocol.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_SYSCALL")" \
            "sw/include/astra/syscall.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_MESSAGE_ABI")" \
            "sw/include/astra/message_abi.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_LIMITS")" \
            "sw/include/astra/limits.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_BLOCK")" \
            "sw/include/astra/block.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_NETWORK")" \
            "sw/include/astra/network.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_HOST")" \
            "sw/include/astra/host.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_STATUS")" \
            "sw/include/astra/status.h"
        printf '%s  %s\n' "$(sha256_file "$PUBLIC_VFS_SERVICE")" \
            "sw/include/astra/vfs_service.h"
    ) | sha256_stream
}

DEFAULT_CACHE_ROOT=${XDG_CACHE_HOME:-$HOME/.cache}/astra68
DEFAULT_WORK_ROOT=$DEFAULT_CACHE_ROOT/qemu-9.2.4
if [ -f /mnt/Documents/astra68/vendor/qemu/$QEMU_ARCHIVE ]; then
    DEFAULT_ARCHIVE=/mnt/Documents/astra68/vendor/qemu/$QEMU_ARCHIVE
else
    DEFAULT_ARCHIVE=$DEFAULT_CACHE_ROOT/vendor/qemu/$QEMU_ARCHIVE
fi

WORK_ROOT=${ASTRA_QEMU_WORK_ROOT:-$DEFAULT_WORK_ROOT}
ARCHIVE=${ASTRA_QEMU_ARCHIVE:-$DEFAULT_ARCHIVE}
case "$WORK_ROOT" in
    "$REPOSITORY"|"$REPOSITORY"/*)
        echo "QEMU work root must be outside the Git repository: $WORK_ROOT" >&2
        exit 2
        ;;
esac

OVERLAY_SHA256=$(overlay_identity)
SOURCE="$WORK_ROOT/source-$OVERLAY_SHA256"
IDENTITY="$SOURCE/.astra-source-identity"

if [ -f "$IDENTITY" ]; then
    printf '%s\n' "$SOURCE"
    exit 0
fi
if [ -e "$SOURCE" ]; then
    echo "Unidentified QEMU source already exists: $SOURCE" >&2
    exit 2
fi

mkdir -p "$WORK_ROOT" "$(dirname -- "$ARCHIVE")"
if [ ! -f "$ARCHIVE" ]; then
    PARTIAL="$ARCHIVE.partial.$$"
    trap 'rm -f "$PARTIAL"' EXIT HUP INT TERM
    curl --fail --location --output "$PARTIAL" "$QEMU_URL"
    mv "$PARTIAL" "$ARCHIVE"
    trap - EXIT HUP INT TERM
fi

ACTUAL_ARCHIVE_SHA256=$(sha256_file "$ARCHIVE")
if [ "$ACTUAL_ARCHIVE_SHA256" != "$QEMU_ARCHIVE_SHA256" ]; then
    echo "QEMU archive SHA-256 mismatch" >&2
    echo "expected: $QEMU_ARCHIVE_SHA256" >&2
    echo "actual:   $ACTUAL_ARCHIVE_SHA256" >&2
    exit 2
fi

STAGE="$WORK_ROOT/.prepare-$OVERLAY_SHA256-$$"
trap 'rm -rf "$STAGE"' EXIT HUP INT TERM
mkdir -p "$STAGE"
tar -xJf "$ARCHIVE" -C "$STAGE"
STAGED_SOURCE="$STAGE/qemu-$QEMU_VERSION"

for required in configure meson.build hw/m68k/meson.build target/m68k/cpu.c; do
    if [ ! -f "$STAGED_SOURCE/$required" ]; then
        echo "Incomplete QEMU extraction: missing $required" >&2
        exit 2
    fi
done

cp "$OVERLAY/hw/m68k/astra68.c" "$STAGED_SOURCE/hw/m68k/astra68.c"
cp "$OVERLAY/target/m68k/astra_pmmu030.c" "$STAGED_SOURCE/target/m68k/astra_pmmu030.c"
cp "$OVERLAY/target/m68k/pmmu030.c" "$STAGED_SOURCE/target/m68k/pmmu030.c"
cp "$OVERLAY/target/m68k/pmmu030.h" "$STAGED_SOURCE/target/m68k/pmmu030.h"
cp "$PUBLIC_INPUT" "$STAGED_SOURCE/include/hw/m68k/astra_input.h"
cp "$PUBLIC_DISPLAY_MAILBOX" \
    "$STAGED_SOURCE/include/hw/m68k/astra_display_mailbox.h"
cp "$PUBLIC_RENDER_BATCH" \
    "$STAGED_SOURCE/include/hw/m68k/astra_render_batch.h"
cp "$PUBLIC_RENDER_PROTOCOL" \
    "$STAGED_SOURCE/include/hw/m68k/astra_render_protocol.h"
mkdir -p "$STAGED_SOURCE/include/astra"
cp "$PUBLIC_DISPLAY" "$STAGED_SOURCE/include/astra/display.h"
cp "$PUBLIC_SYSCALL" "$STAGED_SOURCE/include/astra/syscall.h"
cp "$PUBLIC_MESSAGE_ABI" "$STAGED_SOURCE/include/astra/message_abi.h"
cp "$PUBLIC_LIMITS" "$STAGED_SOURCE/include/astra/limits.h"
cp "$PUBLIC_BLOCK" "$STAGED_SOURCE/include/astra/block.h"
cp "$PUBLIC_NETWORK" "$STAGED_SOURCE/include/astra/network.h"
cp "$PUBLIC_HOST" "$STAGED_SOURCE/include/astra/host.h"
cp "$PUBLIC_STATUS" "$STAGED_SOURCE/include/astra/status.h"
cp "$PUBLIC_VFS_SERVICE" "$STAGED_SOURCE/include/astra/vfs_service.h"
ln -s include/astra "$STAGED_SOURCE/astra"
patch -d "$STAGED_SOURCE" -p1 --forward < "$OVERLAY/meson.build.patch" >&2
patch -d "$STAGED_SOURCE" -p1 --forward < "$OVERLAY/target-m68k-pmmu030.patch" >&2
patch -d "$STAGED_SOURCE" -p1 --forward < "$OVERLAY/target-m68k-68030-frames.patch" >&2

{
    printf 'qemu_version=%s\n' "$QEMU_VERSION"
    printf 'archive_sha256=%s\n' "$QEMU_ARCHIVE_SHA256"
    printf 'overlay_sha256=%s\n' "$OVERLAY_SHA256"
} > "$STAGED_SOURCE/.astra-source-identity"

mv "$STAGED_SOURCE" "$SOURCE"
rm -rf "$STAGE"
trap - EXIT HUP INT TERM
printf '%s\n' "$SOURCE"
