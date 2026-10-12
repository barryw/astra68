#!/bin/sh
# The filesystem gates (plan docs/superpowers/plans/2026-10-07-filesystem-phase1.md).
# usage: fs-gates.sh QEMU ROM IMAGE   -- run from the repository root on beast
set -u
Q=$1 R=$2 I=$3
make -C emu/qemu/tests test > /tmp/mg-fs-resolve.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-resolve.log
bash tools/tests/test_astra_qemu_root_helper.sh /usr/local/sbin/astra-qemu-root \
    > /tmp/mg-fs-root-helper.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-root-helper.log
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
# HOST_DEVICE holders cannot mint filesystem scopes (Task 3b).
cp "$I" /tmp/mg-fs-hostbench.img
python3 emu/qemu/astra_image.py --hostbench /tmp/mg-fs-hostbench.img \
    > /tmp/mg-fs-hostbench.log 2>&1 &&
python3 emu/qemu/test-host-channel-userspace.py "$Q" "$R" /tmp/mg-fs-hostbench.img \
    --work /tmp/mg-fs-hcu-work --host-work /tmp/mg-fs-hcu-host \
    >> /tmp/mg-fs-hostbench.log 2>&1
echo "EXIT=$?" >> /tmp/mg-fs-hostbench.log
rm -f /tmp/mg-fs-hostbench.img
for f in fs-resolve fs-root-helper fs-hostfs fs-semantics fs-stress fs-hostbench; do
    echo "== $f"
    tail -4 /tmp/mg-$f.log
done
