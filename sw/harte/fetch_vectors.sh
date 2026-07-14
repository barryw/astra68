#!/bin/bash
set -euo pipefail

REPOSITORY=https://github.com/SingleStepTests/m68000.git
REVISION=64b253116a3de04aaac4346c43680960dc9b67e5
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
DESTINATION=${HARTE_DATA_DIR:-$ROOT/sw/harte/data/m68000-$REVISION}
VECTOR_DIR=$DESTINATION/v1

if [ ! -d "$DESTINATION/.git" ]; then
    mkdir -p "$(dirname "$DESTINATION")"
    git clone --filter=blob:none --sparse "$REPOSITORY" "$DESTINATION"
    git -C "$DESTINATION" sparse-checkout set v1
fi

git -C "$DESTINATION" fetch --filter=blob:none origin "$REVISION"
git -C "$DESTINATION" checkout --detach "$REVISION"

ACTUAL=$(git -C "$DESTINATION" rev-parse HEAD)
if [ "$ACTUAL" != "$REVISION" ]; then
    echo "Harte corpus revision mismatch: got $ACTUAL, expected $REVISION" >&2
    exit 1
fi

printf 'SingleStepTests/m68000@%s\n' "$REVISION" > "$DESTINATION/.astra-harte-revision"
COUNT=$(find "$VECTOR_DIR" -maxdepth 1 -type f -name '*.json.bin' | wc -l)
if [ "$COUNT" -ne 127 ]; then
    echo "Harte corpus is incomplete: found $COUNT files, expected 127" >&2
    exit 1
fi

echo "$VECTOR_DIR"
