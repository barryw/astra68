#!/bin/sh
set -eu

target=${1:-all}
case "$target" in
    all|html|pdf|check) ;;
    *)
        echo "usage: $0 [all|html|pdf|check]" >&2
        exit 2
        ;;
esac

ndk_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ "${ASTRA_NDK_DOCS_NATIVE:-0}" = 1 ]; then
    cd "$ndk_dir"
    exec sh docs/build-native.sh "$target"
fi

image=${ASTRA_NDK_DOCS_IMAGE:-astra68-ndk-docs:8.2.3-1}
build_network=${ASTRA_NDK_DOCS_BUILD_NETWORK:-host}
docker build --network "$build_network" --tag "$image" "$ndk_dir/docs"
exec docker run --rm \
    --network none \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env XDG_CACHE_HOME=/tmp/.cache \
    --volume "$ndk_dir:/workspace" \
    --workdir /workspace \
    "$image" sh docs/build-native.sh "$target"
