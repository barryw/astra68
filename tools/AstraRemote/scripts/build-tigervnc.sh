#!/bin/sh
set -eu

revision=dd416cbfa2023ffcdd3bd23d90ce8254b3faf627
archive_sha256=bf4f7c0c206148973bcf3420ba1e5fe7107fa2848bbff7088ac42d5423eae138
root="${DERIVED_FILE_DIR:?}/tigervnc"
source_dir="$root/source"
build_dir="$root/build"
set -- ${ARCHS:-}
target_arch="${1:-$(uname -m)}"

# Xcode's script environment injects compiler flags containing undefined_arch.
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS

if [ ! -e "$source_dir/.astra-revision" ]; then
    if [ -e "$source_dir" ]; then
        echo "Incomplete TigerVNC source in $source_dir; remove it before rebuilding" >&2
        exit 1
    fi
    mkdir -p "$root"
    curl --fail --location --silent --show-error \
        "https://github.com/TigerVNC/tigervnc/archive/$revision.tar.gz" \
        --output "$root/source.tar.gz"
    actual_sha256="$(shasum -a 256 "$root/source.tar.gz" | cut -d ' ' -f 1)"
    if [ "$actual_sha256" != "$archive_sha256" ]; then
        echo "TigerVNC archive checksum mismatch" >&2
        exit 1
    fi
    staging="$(mktemp -d "$root/source.XXXXXX")"
    trap 'rm -rf "$staging"' EXIT HUP INT TERM
    tar -xzf "$root/source.tar.gz" -C "$staging" --strip-components=1
    printf '%s\n' "$revision" > "$staging/.astra-revision"
    mv "$staging" "$source_dir"
    trap - EXIT HUP INT TERM
fi
if [ "$(sed -n '1p' "$source_dir/.astra-revision")" != "$revision" ]; then
    echo "TigerVNC revision mismatch in $source_dir" >&2
    exit 1
fi

cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_OSX_ARCHITECTURES="$target_arch" \
    -DBUILD_VIEWER=OFF -DENABLE_NLS=OFF -DENABLE_H264=OFF \
    -DENABLE_AUDIO=OFF -DENABLE_GNUTLS=OFF -DENABLE_NETTLE=OFF
cmake --build "$build_dir" --target rfbclient network --parallel
