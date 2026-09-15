#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
WORKSPACE=$(mktemp -d "${TMPDIR:-/tmp}/astra-de25-release.XXXXXX")
RELEASE=$WORKSPACE/release
STORAGE=$WORKSPACE/storage-terminal.img
SOURCE_BEFORE=$WORKSPACE/source-before.sha256
SOURCE_AFTER=$WORKSPACE/source-after.sha256
SOURCE_GIT=$WORKSPACE/source.git
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')
DE25_SYSROOT=${DE25_SYSROOT:-${XDG_CACHE_HOME:-"$HOME/.cache"}/astra68/de25-jammy-arm64}
PYTHONPYCACHEPREFIX=$WORKSPACE/pycache
export PYTHONPYCACHEPREFIX

source_manifest() {
    python3 - "$REPOSITORY" "$SOURCE_GIT" <<'PY'
import hashlib
import os
from pathlib import Path
import stat
import subprocess
import sys

root = Path(sys.argv[1])
git_dir = Path(sys.argv[2])
listed = subprocess.check_output([
    "git", "--git-dir", str(git_dir), "--work-tree", str(root), "ls-files",
    "-z", "--others", "--exclude-standard",
])
for name in sorted(filter(None, listed.decode("utf-8").split("\0"))):
    relative = Path(name)
    path = root / relative
    if path.is_symlink():
        payload = b"symlink\0" + os.readlink(path).encode("utf-8")
        mode = "l"
    elif path.is_file():
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1 << 20), b""):
                digest.update(block)
        payload = None
        mode = "x" if path.stat().st_mode & stat.S_IXUSR else "-"
    else:
        payload = b"missing\0"
        mode = "!"
    value = hashlib.sha256(payload).hexdigest() if payload is not None else digest.hexdigest()
    print(f"{value}  {mode}  {relative.as_posix()}")
PY
}

cleanup() {
    status=$?
    # Created releases are deliberately read-only. The temporary owner must
    # unlock its own tree before removing it on success, failure, or signal.
    chmod -R u+w "$WORKSPACE" 2>/dev/null || true
    rm -rf -- "$WORKSPACE"
    exit "$status"
}
trap cleanup EXIT HUP INT TERM

git init --bare -q "$SOURCE_GIT"
source_manifest >"$SOURCE_BEFORE"

# Production publication owns every software input. Clean only those producers;
# the independently qualified FPGA route is not this publisher's artifact.
make -C "$REPOSITORY/sw/userspace" clean
make -C "$REPOSITORY/sw/boot" clean
make -C "$REPOSITORY/ndk" clean
make -C "$REPOSITORY/tools" clean
make -j "$JOBS" -C "$REPOSITORY/sw/userspace" all
make -j "$JOBS" -C "$REPOSITORY/sw/boot" build/astra_boot.bin
python3 "$SCRIPT_DIR/astra_image.py" --create "$STORAGE" 128
QEMU=$($SCRIPT_DIR/build.sh de25)

source_manifest >"$SOURCE_AFTER"
cmp "$SOURCE_BEFORE" "$SOURCE_AFTER"

ASTRA_DE25_QEMU=$QEMU \
ASTRA_DE25_ROM=$REPOSITORY/sw/boot/build/astra_boot.bin \
ASTRA_DE25_STORAGE=$STORAGE \
ASTRA_DE25_QEMU_LIBDIR=$DE25_SYSROOT/usr/lib/aarch64-linux-gnu \
ASTRA_DE25_SOURCE_MANIFEST=$SOURCE_AFTER \
    "$SCRIPT_DIR/create-de25-release.sh" "$RELEASE"
"$SCRIPT_DIR/deploy-de25-release.sh" "$RELEASE"
