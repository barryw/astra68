#!/bin/sh
# The filesystem gates (plan docs/superpowers/plans/2026-10-07-filesystem-phase1.md).
# usage: fs-gates.sh QEMU ROM IMAGE   -- run from the repository root on beast
set -u
Q=$1 R=$2 I=$3
bash tools/tests/test_astra_qemu_root_paths.sh /usr/local/sbin/astra-qemu-root \
    > /tmp/mg-fs-root-paths.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-root-paths.log
python3 emu/qemu/test-hostfs.py "$Q" > /tmp/mg-fs-hostfs.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-hostfs.log
python3 emu/qemu/test-fs-semantics.py "$Q" "$R" "$I" > /tmp/mg-fs-semantics.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-semantics.log
rm -rf /tmp/astra-fs-stress /tmp/astra-fs-stress-host
python3 emu/qemu/test-filesystem-stress.py "$Q" "$R" "$I" \
    --e2fsck /usr/sbin/e2fsck \
    --work /tmp/astra-fs-stress --host-work /tmp/astra-fs-stress-host \
    --operations 400 > /tmp/mg-fs-stress.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-stress.log
for f in fs-root-paths fs-hostfs fs-semantics fs-stress; do
    echo "== $f"
    tail -4 /tmp/mg-$f.log
done
