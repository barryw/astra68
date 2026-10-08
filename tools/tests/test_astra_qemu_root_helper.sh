#!/bin/bash
# Regression test for tools/astra-qemu-root: QEMU runs as the caller with
# no privilege left, root acts only inside its own /var/tmp directory R
# (never in a directory the caller can modify), the image read is bounded,
# and nothing outlives the run. Linux-only (loop mount, sudo, setpriv);
# run on beast, never on the Mac.
#
#   bash tools/tests/test_astra_qemu_root_helper.sh [/path/to/astra-qemu-root]
#
# Defaults to the repository copy. The caller needs passwordless sudo for
# the helper and, for setup and the perturbation copies, in general (on
# beast barry has both). Perturbed helpers are installed in a root-owned
# mktemp directory under /var/tmp and removed at exit; nothing is
# installed in /usr/local/sbin.
set -euo pipefail

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
WRAPPER=${1:-$HERE/../astra-qemu-root}
CALLER_UID=$(id -u)
CALLER_GID=$(id -g)
WORK=$(mktemp -d /tmp/astra-qemu-root-helper-test.XXXXXX)
chmod 700 "$WORK"
PERTURB=
PASS=0
FAIL=0
SERIAL=0

cleanup() {
    local status=$?
    [ -n "$PERTURB" ] && sudo -n rm -rf -- "$PERTURB"
    rm -rf -- "$WORK"
    exit "$status"
}
trap cleanup EXIT

note_pass() { echo "PASS $*"; PASS=$((PASS + 1)); }
note_fail() { echo "FAIL $*"; FAIL=$((FAIL + 1)); }

# A provider image exactly as qemu_runtime.root_provider builds one:
# work/ and sound/ made at mkfs time, the filesystem root owned by this
# caller. `seed_work` replaces work/ with a symlink to that target.
make_provider() {
    local label=$1 seed_work=${2:-}
    local dir="$WORK/$label"
    mkdir -p -- "$dir/seed/sound"
    if [ -n "$seed_work" ]; then
        ln -s -- "$seed_work" "$dir/seed/work"
    else
        mkdir -- "$dir/seed/work"
    fi
    truncate -s 64M -- "$dir/image.img"
    /usr/sbin/mke2fs -q -F -t ext4 -O "^orphan_file" \
        -E "root_owner=$CALLER_UID:$CALLER_GID" \
        -d "$dir/seed" "$dir/image.img"
    echo "$dir/image.img"
}

helper_dirs() { ls -d /var/tmp/astra-qemu-root.* 2>/dev/null | sort || true; }

# Runs WRAPPER on IMAGE in the background with a stand-in command, log in
# LOG. Sets BG to its pid.
start() {
    local wrapper=$1 image=$2 log=$3
    shift 3
    sudo -n "$wrapper" --image "$image" -- "$@" > "$log" 2>&1 &
    BG=$!
}

# The R the helper announced in LOG (empty if it never did within 10 s).
announced_r() {
    local log=$1 end=$((SECONDS + 10)) line
    while [ "$SECONDS" -le "$end" ]; do
        line=$(grep -m1 '^ASTRA-QEMU-ROOT-MOUNT ' "$log" 2>/dev/null || true)
        if [ -n "$line" ]; then
            dirname -- "${line#ASTRA-QEMU-ROOT-MOUNT }"
            return
        fi
        kill -0 "$BG" 2>/dev/null || break
        sleep 0.05
    done
}

finish() { if wait "$BG"; then STATUS=0; else STATUS=$?; fi; }

# --- N6: the running stand-in holds no privilege at all ------------------
#
# Polls (the exec chain passes through root on its way down) for all four
# Uid: fields == caller, all four Gid: fields == caller gid, CapEff and
# CapPrm zero. Prints "yes" or "no LAST-SEEN"; never PASS/FAIL itself, so
# a perturbation that expects "no" prints no bare FAIL token.
observe_creds() {
    local wrapper=$1 label=$2
    local image r pid want seen=MISSING
    image=$(make_provider "creds-$label")
    start "$wrapper" "$image" "$WORK/creds-$label.log" /bin/sleep 3
    r=$(announced_r "$WORK/creds-$label.log")
    if [ -z "$r" ]; then
        finish
        echo "no NO-ANNOUNCE($(tr '\n' ' ' < "$WORK/creds-$label.log"))"
        return
    fi
    local end=$((SECONDS + 5))
    pid=
    while [ -z "$pid" ] && [ "$SECONDS" -le "$end" ]; do
        pid=$(cat "$r/qemu.pid" 2>/dev/null || true)
        [ -n "$pid" ] || sleep 0.05
    done
    want="$CALLER_UID $CALLER_UID $CALLER_UID $CALLER_UID|$CALLER_GID $CALLER_GID $CALLER_GID $CALLER_GID|0000000000000000|0000000000000000"
    while [ -n "$pid" ] && [ "$SECONDS" -le "$end" ]; do
        local now
        now=$(awk '/^Uid:/{u=$2" "$3" "$4" "$5} /^Gid:/{g=$2" "$3" "$4" "$5}
                   /^CapEff:/{e=$2} /^CapPrm:/{p=$2}
                   END{print u"|"g"|"e"|"p}' "/proc/$pid/status" 2>/dev/null || true)
        [ -n "$now" ] && [ "$now" != "|||" ] || break
        seen=$now
        [ "$seen" = "$want" ] && break
        sleep 0.05
    done
    finish
    if [ "$seen" = "$want" ]; then echo yes; else echo "no $seen"; fi
}

run_test_creds() {
    local result
    result=$(observe_creds "$WRAPPER" fixed)
    if [ "$result" = yes ]; then
        note_pass "fs-root-helper creds: stand-in ran with uid/gid all $CALLER_UID/$CALLER_GID, CapEff=CapPrm=0"
    else
        note_fail "fs-root-helper creds: expected uid/gid $CALLER_UID/$CALLER_GID and no capabilities, saw ${result#no }"
    fi
}

# --- a symlinked, unreadable or non-regular image is refused ------------

expect_refusal() {
    local name=$1 want=$2 image=$3 output status
    output=$(timeout 30 sudo -n "$WRAPPER" --image "$image" -- /bin/true 2>&1) &&
        status=0 || status=$?
    if [ "$status" -eq 64 ] && [[ "$output" == *"$want"* ]]; then
        note_pass "fs-root-helper $name: refused ($(echo "$output" | tr '\n' ' '))"
    else
        note_fail "fs-root-helper $name: expected refusal '$want', got status=$status $output"
    fi
}

run_test_images() {
    ln -s /etc/shadow "$WORK/shadow-link"
    expect_refusal image-is-symlink "not a symlink" "$WORK/shadow-link"

    local image
    image=$(make_provider unreadable)
    sudo -n chown root:root "$image"
    sudo -n chmod 600 "$image"
    expect_refusal image-caller-cannot-read "could not read image as the caller" "$image"

    mkfifo "$WORK/fifo.img"
    expect_refusal image-is-fifo "regular file" "$WORK/fifo.img"
}

# --- N7: an image growing during the read is copied at its open size ---
#
# A sparse appender grows IMAGE far faster than anything can read it, from
# before the helper starts until it announces the mount (the copy is done
# by then). An unbounded reader (cat) would chase it to the appender's
# cap; the bounded one copies the size it saw at open. Refusal for
# exceeding /var/tmp's free space is also acceptable; unbounded is not.
run_test_growing() {
    local image r copied final cap=$((64 * 1024 * 1024 * 1024))
    image=$(make_provider growing)
    touch "$WORK/grow.run"
    ( while [ -e "$WORK/grow.run" ] && [ "$(stat -c %s "$image")" -lt "$cap" ]; do
          truncate -s +1M "$image"
      done ) &
    local grower=$!
    sleep 0.2
    start "$WRAPPER" "$image" "$WORK/growing.log" /bin/sleep 2
    r=$(announced_r "$WORK/growing.log")
    rm -f "$WORK/grow.run"
    wait "$grower" || true
    if [ -z "$r" ]; then
        finish
        if [ "$STATUS" -eq 64 ] && grep -q "exceeds the" "$WORK/growing.log"; then
            note_pass "fs-root-helper image-growing: refused for size ($(tail -1 "$WORK/growing.log"))"
        else
            note_fail "fs-root-helper image-growing: no mount announced, status $STATUS: $(cat "$WORK/growing.log")"
        fi
        return
    fi
    copied=$(sudo -n stat -c %s "$r/img" 2>/dev/null || echo GONE)
    final=$(stat -c %s "$image")
    finish
    if [ "$copied" != GONE ] && [ "$copied" -ge $((64 * 1024 * 1024)) ] &&
            [ "$copied" -lt "$final" ]; then
        note_pass "fs-root-helper image-growing: copied $copied bytes, image had grown to $final"
    else
        note_fail "fs-root-helper image-growing: copied $copied bytes, image grew to $final"
    fi
}

# --- planted symlinks in the caller's directory are never followed ------
#
# Plants every name earlier helpers wrote beside the image -- qemu.pid,
# a scratch copy, a mount directory -- as symlinks to decoys this test
# owns. Prints "unchanged" or "changed WHAT"; never PASS/FAIL itself.
observe_planted() {
    local wrapper=$1 label=$2
    local image dir decoy before after
    image=$(make_provider "planted-$label")
    dir=$(dirname -- "$image")
    decoy="$WORK/decoy-$label"
    mkdir -- "$decoy" "$decoy/dir"
    echo "decoy contents" > "$decoy/file"
    chmod 600 -- "$decoy/file"
    ln -s -- "$decoy/file" "$dir/qemu.pid"
    ln -s -- "$decoy/file" "$dir/astra-qemu-root-scratch.AAAAAA"
    ln -s -- "$decoy/dir" "$dir/mount"
    ln -s -- "$decoy/dir" "$dir/provider"
    before=$(stat -c '%a %s %Y %h' "$decoy/file" "$decoy/dir"; cat "$decoy/file")
    start "$wrapper" "$image" "$WORK/planted-$label.log" /bin/sleep 1
    finish
    after=$(stat -c '%a %s %Y %h' "$decoy/file" "$decoy/dir"; cat "$decoy/file")
    if [ "$STATUS" -ne 0 ]; then
        echo "changed wrapper-exit-$STATUS"
    elif [ "$before" = "$after" ] && [ -z "$(ls -A -- "$decoy/dir")" ]; then
        echo unchanged
    else
        echo "changed $(echo "$after" | tr '\n' ' ')"
    fi
}

run_test_planted() {
    local result
    result=$(observe_planted "$WRAPPER" fixed)
    if [ "$result" = unchanged ]; then
        note_pass "fs-root-helper planted-symlinks: decoys behind qemu.pid/scratch/mount links unchanged"
    else
        note_fail "fs-root-helper planted-symlinks: ${result#changed }"
    fi
}

# --- a provider whose `work` is a symlink is refused --------------------

run_test_work_symlink() {
    local image before after
    image=$(make_provider work-etc /etc)
    before=$(stat -c '%U:%G:%a' /etc)
    expect_refusal work-is-symlink-to-etc "symlink" "$image"
    after=$(stat -c '%U:%G:%a' /etc)
    if [ "$before" = "$after" ]; then
        note_pass "fs-root-helper work-is-symlink-to-etc: /etc unchanged ($after)"
    else
        note_fail "fs-root-helper work-is-symlink-to-etc: /etc CHANGED $before -> $after"
    fi
}

# --- R, the mount and the loop device are gone after success and failure

check_gone() {
    local name=$1 r=$2
    if [ -z "$r" ]; then
        note_fail "fs-root-helper cleanup ($name): no mount announced"
    elif sudo -n test -e "$r"; then
        note_fail "fs-root-helper cleanup ($name): $r still exists"
    elif findmnt -rn -o TARGET | grep -q '^/var/tmp/astra-qemu-root\.'; then
        note_fail "fs-root-helper cleanup ($name): a helper mount remains"
    else
        note_pass "fs-root-helper cleanup ($name): $r removed, no helper mount"
    fi
}

run_test_cleanup() {
    local image r
    image=$(make_provider cleanup-success)
    start "$WRAPPER" "$image" "$WORK/cleanup-success.log" /bin/sleep 1
    r=$(announced_r "$WORK/cleanup-success.log")
    finish
    [ "$STATUS" -eq 0 ] || note_fail "fs-root-helper cleanup (success): wrapper exit $STATUS"
    check_gone success "$r"

    image=$(make_provider cleanup-failure)
    start "$WRAPPER" "$image" "$WORK/cleanup-failure.log" /bin/sh -c 'exit 7'
    r=$(announced_r "$WORK/cleanup-failure.log")
    finish
    if [ "$STATUS" -eq 7 ]; then
        note_pass "fs-root-helper cleanup (failure): wrapper propagated exit 7"
    else
        note_fail "fs-root-helper cleanup (failure): expected exit 7, got $STATUS"
    fi
    check_gone failure "$r"
}

# --- a control: a specific marker round-trips through the write-back ----

run_test_control() {
    local image marker
    image=$(make_provider control)
    start "$WRAPPER" "$image" "$WORK/control.log" /bin/bash -c \
        'echo fs-root-helper-marker-$$ > "$ASTRA_HOSTFS_ROOT/work/marker.txt"'
    finish
    if [ "$STATUS" -ne 0 ]; then
        note_fail "fs-root-helper control: wrapper exited $STATUS: $(cat "$WORK/control.log")"
        return
    fi
    marker=$(/usr/sbin/debugfs -R 'cat /work/marker.txt' "$image" 2>/dev/null | tr -d '\r\n')
    if [[ "$marker" == fs-root-helper-marker-* ]]; then
        note_pass "fs-root-helper control: marker '$marker' round-tripped through the mount and the write-back"
    else
        note_fail "fs-root-helper control: expected a fs-root-helper-marker-* line in the written-back image, got '$marker'"
    fi
}

# --- perturbations: scratch copies, root-owned, never the installed one --
#
# Each must make its case fail. The case's own observe_* prints nothing,
# so an expected failure here is one PASS line, never a bare FAIL token.
perturbed() {
    local name=$1 from=$2 to=$3
    sed "s|$from|$to|" "$WRAPPER" > "$WORK/perturb-$name" || return 1
    local differ=0
    cmp -s "$WRAPPER" "$WORK/perturb-$name" || differ=$?
    if [ "$differ" -ne 1 ]; then
        echo "perturbation $name matched nothing" >&2
        return 1
    fi
    sudo -n install -o root -g root -m 0755 "$WORK/perturb-$name" "$PERTURB/$name"
    echo "$PERTURB/$name"
}

run_perturbations() {
    local helper result
    PERTURB=$(sudo -n mktemp -d /var/tmp/astra-qemu-root-test-perturb.XXXXXX)
    if helper=$(perturbed pidfile-in-caller-dir \
            'PIDFILE=$R/qemu.pid' 'PIDFILE=$(dirname -- "$IMAGE")/qemu.pid'); then
        result=$(observe_planted "$helper" perturbed)
        if [ "$result" != unchanged ]; then
            note_pass "fs-root-helper perturbation (pidfile in caller dir): planted-symlinks case caught it (${result#changed })"
        else
            note_fail "fs-root-helper perturbation (pidfile in caller dir): planted-symlinks case did NOT notice -- no teeth"
        fi
    else
        note_fail "fs-root-helper perturbation (pidfile in caller dir): could not build"
    fi

    # Drops the uid but leaves the gid root: a check of the real uid alone
    # passes this. (`--ruid` is no perturbation: util-linux 2.39 setpriv
    # sets all four uids from it, and the next exec clears the caps.)
    if helper=$(perturbed uid-only \
            ' --regid="$SUDO_GID"' ''); then
        result=$(observe_creds "$helper" perturbed)
        if [ "$result" != yes ]; then
            note_pass "fs-root-helper perturbation (gid left root): creds case caught it (${result#no })"
        else
            note_fail "fs-root-helper perturbation (gid left root): creds case did NOT notice -- no teeth"
        fi
    else
        note_fail "fs-root-helper perturbation (gid left root): could not build"
    fi
}

LOOP_BEFORE=$(sudo -n losetup -a | wc -l)
DIRS_BEFORE=$(helper_dirs)

run_test_creds
run_test_images
run_test_growing
run_test_planted
run_test_work_symlink
run_test_cleanup
run_test_control
run_perturbations

LOOP_AFTER=$(sudo -n losetup -a | wc -l)
if [ "$LOOP_BEFORE" = "$LOOP_AFTER" ]; then
    note_pass "fs-root-helper loop devices: count unchanged ($LOOP_AFTER)"
else
    note_fail "fs-root-helper loop devices: count changed $LOOP_BEFORE -> $LOOP_AFTER"
fi
DIRS_AFTER=$(helper_dirs)
if [ "$DIRS_BEFORE" = "$DIRS_AFTER" ]; then
    note_pass "fs-root-helper /var/tmp: no helper directory left behind"
else
    note_fail "fs-root-helper /var/tmp: helper directories changed: $DIRS_BEFORE -> $DIRS_AFTER"
fi

echo "ASTRA-QEMU-ROOT-HELPER-TEST passed $PASS failed $FAIL"
[ "$FAIL" -eq 0 ]
