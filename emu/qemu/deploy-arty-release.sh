#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 RELEASE_DIRECTORY" >&2
    exit 2
fi
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
RELEASE=$1
BOARD=${ASTRA_ARTY_BOARD:-root@192.168.1.188}
SSH=${SSH:-ssh}
SCP=${SCP:-scp}
RELEASE_TOOL=$REPOSITORY/tools/astra_release.py
IDENTITY=$(PYTHONDONTWRITEBYTECODE=1 \
    python3 "$RELEASE_TOOL" verify "$RELEASE")
INCOMING=$($SSH "$BOARD" \
    "mkdir -p /data/astra/incoming && mktemp -d /data/astra/incoming/release.XXXXXX")

cleanup() {
    status=$?
    if [ -n "$INCOMING" ]; then
        $SSH "$BOARD" "rm -rf '$INCOMING'" >/dev/null 2>&1 || true
    fi
    exit "$status"
}
trap cleanup EXIT
$SCP -r "$RELEASE/." "$BOARD:$INCOMING/"
INSTALLED=$($SSH "$BOARD" \
    "PYTHONDONTWRITEBYTECODE=1 python3 '$INCOMING/bin/astra-release.py' \
install '$INCOMING' /data/astra")
if [ "$INSTALLED" != "$IDENTITY" ]; then
    echo "installed Astra release identity changed" >&2
    exit 1
fi
ACTIVE=$($SSH "$BOARD" \
    "PYTHONDONTWRITEBYTECODE=1 python3 \
/data/astra/current/bin/astra-release.py verify --installed \
/data/astra/current")
if [ "$ACTIVE" != "$IDENTITY" ]; then
    echo "active Astra release identity changed" >&2
    exit 1
fi
INCOMING=
echo "ASTRA_ARTY_RELEASE PASS release=$IDENTITY"
