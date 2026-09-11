#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
WORKSPACE=$(mktemp -d "${TMPDIR:-/tmp}/astra-de25-release.XXXXXX")
RELEASE=$WORKSPACE/release

cleanup() {
    status=$?
    # Created releases are deliberately read-only. The temporary owner must
    # unlock its own tree before removing it on success, failure, or signal.
    chmod -R u+w "$WORKSPACE" 2>/dev/null || true
    rm -rf -- "$WORKSPACE"
    exit "$status"
}
trap cleanup EXIT HUP INT TERM

"$SCRIPT_DIR/create-de25-release.sh" "$RELEASE"
"$SCRIPT_DIR/deploy-de25-release.sh" "$RELEASE"
