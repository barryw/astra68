#!/bin/sh
# Run from repository root on Beast. Images and exports are disposable fixtures.
# usage: fs-gates.sh QEMU ROM IMAGE PROVIDER NATIVE_TEST_CLIENT
set -eu
[ "$#" -eq 5 ] || {
    echo "usage: fs-gates.sh QEMU ROM IMAGE PROVIDER NATIVE_TEST_CLIENT" >&2
    exit 2
}
gate_qemu=$(readlink -f "$1")
gate_rom=$(readlink -f "$2")
gate_image=$(readlink -f "$3")
gate_provider=$(readlink -f "$4")
gate_client=$(readlink -f "$5")
gate_uid=$(id -u)
gate_gid=$(id -g)
[ "$gate_uid" -gt 0 ] && [ "$gate_gid" -gt 0 ] || {
    echo "fs-gates: invoke as the non-root build/test account" >&2
    exit 2
}
for executable in "$gate_qemu" "$gate_provider" "$gate_client"; do
    [ -x "$executable" ] || { echo "fs-gates: missing executable $executable" >&2; exit 2; }
done
for input in "$gate_rom" "$gate_image"; do
    [ -r "$input" ] || { echo "fs-gates: missing input $input" >&2; exit 2; }
done
gate_work=$(mktemp -d /tmp/astra-fs-gates.XXXXXX)
chmod 0755 "$gate_work"
gate_failed=0
run_gate()
{
    gate_name=$1
    shift
    gate_log=$gate_work/$gate_name.log
    if "$@" >"$gate_log" 2>&1; then
        echo "PASS $gate_name"
        return 0
    else
        gate_status=$?
        gate_failed=$((gate_failed + 1))
        echo "FAIL $gate_name (exit $gate_status): $gate_log" >&2
        tail -12 "$gate_log" >&2
        return 1
    fi
}
# Privileged observers inspect preserved metadata; the provider launches only
# explicit non-root QEMU peers. This is a development fixture, not a sudo API.
observe()
{
    sudo -n env PYTHONDONTWRITEBYTECODE=1 \
        ASTRA_TEST_QEMU_UID="$gate_uid" ASTRA_TEST_QEMU_GID="$gate_gid" \
        ASTRA_TEST_STORAGE_PROVIDER="$gate_provider" "$@"
}
run_gate resolver make -C emu/qemu/tests test || :
run_gate hostile-caller bash tools/tests/test_astra_qemu_root_helper.sh \
    /usr/local/sbin/astra-qemu-root "$gate_client" || :
run_gate hostfs observe python3 -u emu/qemu/test-hostfs.py "$gate_qemu" "$gate_provider" \
    --uid "$gate_uid" --gid "$gate_gid" || :
run_gate semantics observe python3 -u emu/qemu/test-fs-semantics.py \
    "$gate_qemu" "$gate_rom" "$gate_image" || :
run_gate stress observe python3 -u emu/qemu/test-filesystem-stress.py \
    "$gate_qemu" "$gate_rom" "$gate_image" --e2fsck /usr/sbin/e2fsck \
    --work "$gate_work/stress" --host-work "$gate_work/stress-exports" --operations 400 || :
# HOST_DEVICE holders must not mint filesystem scopes.
cp "$gate_image" "$gate_work/hostbench.img"
if run_gate hostbench-image python3 emu/qemu/astra_image.py --hostbench "$gate_work/hostbench.img"; then
    run_gate hostbench observe python3 -u emu/qemu/test-host-channel-userspace.py \
        "$gate_qemu" "$gate_rom" "$gate_work/hostbench.img" \
        --work "$gate_work/hostbench" --host-work "$gate_work/hostbench-exports" || :
fi
printf 'Filesystem evidence: %s; failures: %s\n' "$gate_work" "$gate_failed"
[ "$gate_failed" -eq 0 ]
