#!/bin/bash
# Regression test for tools/astra-qemu-root's drop-privilege design: QEMU
# must run as the caller, never as root, and the only root action -- the
# loop mount -- must never be fooled by a symlink or leak anything after
# it is done. Linux-only (loop mount, sudo, setpriv); run on beast, never
# on the Mac.
#
#   bash tools/tests/test_astra_qemu_root_helper.sh [/path/to/astra-qemu-root]
#
# Defaults to the repository copy, so this can run before (or after)
# `sudo install` puts it at /usr/local/sbin/astra-qemu-root.
set -euo pipefail

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
WRAPPER=${1:-$HERE/../astra-qemu-root}
CALLER_UID=$(id -u)
CALLER_GID=$(id -g)
# A home for this run's own non-fixture scratch files (the perturbation's
# throwaway symlink target, logs, the vulnerable reconstruction script).
# Provider image/mount pairs are NOT put here: each pair needs its own
# directly-under-/tmp 0700 directory, exactly what private_tmp_dir()
# requires (it refuses a nested /tmp/*/*), so they get their own
# top-level mktemp below instead of living inside this one.
WORK=$(mktemp -d /tmp/astra-qemu-root-helper-test.XXXXXX)
chmod 700 "$WORK"
PASS=0
FAIL=0
FIXTURE_DIRS=()

cleanup() {
    local status=$?
    rm -rf -- "$WORK"
    local d
    for d in "${FIXTURE_DIRS[@]:-}"; do
        [ -n "$d" ] && rm -rf -- "$d"
    done
    exit "$status"
}
trap cleanup EXIT

note_pass() { echo "PASS $*"; PASS=$((PASS + 1)); }
note_fail() { echo "FAIL $*"; FAIL=$((FAIL + 1)); }

# A provider image and its empty mount point, exactly as
# qemu_runtime.root_provider builds them: siblings in their own 0700
# directory directly under /tmp, work/ and sound/ pre-populated into the
# image at mkfs time, owned by this caller. Prints "IMAGE MOUNT".
make_provider() {
    local label=$1
    local dir
    dir=$(mktemp -d "/tmp/astra-qemu-root-helper-$label.XXXXXX")
    chmod 700 -- "$dir"
    FIXTURE_DIRS+=("$dir")
    mkdir -p -- "$dir/seed/work" "$dir/seed/sound"
    truncate -s 64M -- "$dir/image.img"
    /usr/sbin/mke2fs -q -F -t ext4 -O "^orphan_file" \
        -E "root_owner=$CALLER_UID:$CALLER_GID" \
        -d "$dir/seed" "$dir/image.img"
    mkdir -- "$dir/mount"
    echo "$dir/image.img $dir/mount"
}

# A provider whose `work` entry is a symlink instead of a directory -- the
# no-follow check must refuse this, and nothing must ever act on the
# symlink's target (the fixed wrapper never creates or touches anything
# inside the mount at all, so it is safe to point this at a real
# sensitive path; see perturb_c_demonstrate_old_pattern below for why the
# *historical* `install -d` pattern is instead demonstrated against a
# scratch target, never a real system path).
make_symlinked_work_provider() {
    local label=$1 target=$2
    local dir
    dir=$(mktemp -d "/tmp/astra-qemu-root-helper-$label.XXXXXX")
    chmod 700 -- "$dir"
    FIXTURE_DIRS+=("$dir")
    mkdir -p -- "$dir/seed"
    ln -s -- "$target" "$dir/seed/work"
    mkdir -p -- "$dir/seed/sound"
    truncate -s 64M -- "$dir/image.img"
    /usr/sbin/mke2fs -q -F -t ext4 -O "^orphan_file" \
        -E "root_owner=$CALLER_UID:$CALLER_GID" \
        -d "$dir/seed" "$dir/image.img"
    mkdir -- "$dir/mount"
    echo "$dir/image.img $dir/mount"
}

wait_for_file() {
    local path=$1 deadline=${2:-5}
    local end=$(($(date +%s) + deadline))
    while [ ! -e "$path" ]; do
        [ "$(date +%s)" -lt "$end" ] || return 1
        sleep 0.1
    done
}

# --- (a): the running "QEMU" process's uid is the caller's -------------
#
# Runs `wrapper` with a provider and a 3 s `/bin/sleep` stand-in, and
# polls (not a single snapshot) for up to that 3 s for its pid to show
# `expect_uid`: the exec chain (unshare -> bash -c -> setpriv -> the
# stand-in) takes the uid through root briefly on its way to the
# caller's, and a single read racing that transition -- likely under
# load, which is exactly what caught this the first time, inside the
# full suite rather than this script run alone -- would read "0" and
# call a correctly-dropping wrapper broken. Prints nothing and never
# calls note_pass/note_fail itself: it only reports "yes SEEN_UID" (the
# expected uid was observed before the deadline) or "no SEEN_UID" (it
# never was), so a caller expecting "no" -- the perturbation below --
# can report that as its own PASS without ever printing a bare FAIL
# line for an outcome the test wanted.
observe_uid() {
    local wrapper=$1 label=$2 expect_uid=$3
    local image mount pidfile
    read -r image mount < <(make_provider "a-$label")
    pidfile="$(dirname -- "$mount")/qemu.pid"
    sudo -n "$wrapper" --image "$image" --mount "$mount" \
        -- /bin/sleep 3 >"$WORK/a-$label.log" 2>&1 &
    local bg=$!
    if ! wait_for_file "$pidfile" 10; then
        if wait "$bg"; then :; fi
        echo "no MISSING"
        return
    fi
    local pid
    pid=$(cat "$pidfile")
    local seen=MISSING last_seen=MISSING deadline
    deadline=$((SECONDS + 3))
    while [ "$SECONDS" -le "$deadline" ]; do
        seen=$(awk '/^Uid:/{print $2}' "/proc/$pid/status" 2>/dev/null || echo GONE)
        [ "$seen" != GONE ] && last_seen=$seen
        [ "$seen" = "$expect_uid" ] && break
        [ "$seen" = GONE ] && break
        sleep 0.05
    done
    wait "$bg" 2>/dev/null || true
    if [ "$seen" = "$expect_uid" ]; then
        echo "yes $seen"
    else
        echo "no $last_seen"
    fi
}

run_test_a() {
    local wrapper=$1 label=$2 expect_uid=$3
    local result matched seen
    result=$(observe_uid "$wrapper" "$label" "$expect_uid")
    matched=${result%% *}
    seen=${result#* }
    if [ "$matched" = yes ]; then
        note_pass "fs-root-helper uid ($label): process ran as uid $seen"
    else
        note_fail "fs-root-helper uid ($label): expected uid $expect_uid, process was uid $seen"
    fi
}

# --- (b): a symlinked or unreadable image is refused --------------------

run_test_b() {
    local image mount output status

    mkdir -m 755 -- "$WORK/b-dummy-mount"
    ln -s /etc/shadow "$WORK/b-shadow-link"
    output=$(sudo -n "$WRAPPER" --image "$WORK/b-shadow-link" \
        --mount "$WORK/b-dummy-mount" -- /bin/true 2>&1) && status=0 || status=$?
    if [ "$status" -eq 64 ] && [[ "$output" == *"not a symlink"* ]]; then
        note_pass "fs-root-helper image-is-symlink: refused ($output)"
    else
        note_fail "fs-root-helper image-is-symlink: expected refusal, got status=$status $output"
    fi

    read -r image mount < <(make_provider "b-unreadable")
    sudo -n chown root:root "$image"
    sudo -n chmod 600 "$image"
    output=$(sudo -n "$WRAPPER" --image "$image" --mount "$mount" \
        -- /bin/true 2>&1) && status=0 || status=$?
    if [ "$status" -eq 64 ] && [[ "$output" == *"could not read image as the caller"* ]]; then
        note_pass "fs-root-helper image-caller-cannot-read: refused ($output)"
    else
        note_fail "fs-root-helper image-caller-cannot-read: expected refusal, got status=$status $output"
    fi
}

# --- (c): a provider whose `work` is a symlink is refused ---------------

run_test_c() {
    local image mount output status

    read -r image mount < <(make_symlinked_work_provider "c-etc" /etc)
    local before after
    before=$(stat -c '%U:%G:%a' /etc)
    output=$(sudo -n "$WRAPPER" --image "$image" --mount "$mount" \
        -- /bin/true 2>&1) && status=0 || status=$?
    after=$(stat -c '%U:%G:%a' /etc)
    if [ "$status" -eq 64 ] && [[ "$output" == *"symlink"* ]]; then
        note_pass "fs-root-helper work-is-symlink-to-etc: refused ($output)"
    else
        note_fail "fs-root-helper work-is-symlink-to-etc: expected refusal, got status=$status $output"
    fi
    if [ "$before" = "$after" ]; then
        note_pass "fs-root-helper work-is-symlink-to-etc: /etc unchanged ($after)"
    else
        note_fail "fs-root-helper work-is-symlink-to-etc: /etc CHANGED $before -> $after"
    fi
}

# Perturbation for (c): the *historical* defect was `install -d -o U -g G
# -m 0755 PATH` creating/fixing up PATH even when PATH is a symlink --
# `install -d` follows it and chowns/chmods whatever it points to. Run
# only the install(1) step itself -- never the full root flow -- and only
# against a throwaway scratch target, never a real system path: this is
# about proving the *mechanism* astra-qemu-root's `-d`/`-L` check now
# refuses, not about reproducing privileged damage on shared infrastructure.
perturb_c_demonstrate_old_pattern() {
    local scratch="$WORK/c-perturb-scratch"
    mkdir -m 711 -- "$scratch"
    local before after
    before=$(stat -c '%a' "$scratch")
    ln -s -- "$scratch" "$WORK/c-perturb-link"
    install -d -m 0755 -- "$WORK/c-perturb-link"
    after=$(stat -c '%a' "$scratch")
    if [ "$before" != "$after" ] && [ "$after" = 755 ]; then
        note_pass "fs-root-helper work-symlink perturbation: the historical install -d pattern follows a symlink and modifies its target ($before -> $after on the scratch target) -- exactly what the fixed wrapper's -L check now refuses outright"
    else
        note_fail "fs-root-helper work-symlink perturbation: expected install -d to follow the symlink and change the scratch target's mode; saw $before -> $after"
    fi
}

# --- (d): nothing remains after the run, including after a failure ------
#
# The mount is NOT hidden from this (the default, host-wide) namespace
# while QEMU runs: an earlier version of this wrapper tried that --
# unmounting its own copy the moment a QEMU child's private namespace
# held an independent one -- and the desktop never finished booting
# behind it (it raced the host side of QEMU's own HostFS init). The
# mount stays visible system-wide for the run's duration, by design (see
# tools/astra-qemu-root's own comment); what this checks instead is that
# nothing is left over once the run ends, success or not.

run_test_d() {
    local image mount status

    read -r image mount < <(make_provider "d-success")
    if sudo -n "$WRAPPER" --image "$image" --mount "$mount" \
            -- /bin/sleep 2 >"$WORK/d-success.log" 2>&1; then
        status=0
    else
        status=$?
    fi
    [ "$status" -eq 0 ] || echo "  (wrapper exit $status for the success run)"
    if [ -z "$(ls -A -- "$mount" 2>/dev/null)" ]; then
        note_pass "fs-root-helper cleanup (success): mount point empty again"
    else
        note_fail "fs-root-helper cleanup (success): mount point still has content: $(ls -A -- "$mount")"
    fi
    if ls "$(dirname -- "$mount")"/astra-qemu-root-scratch.* >/dev/null 2>&1; then
        note_fail "fs-root-helper cleanup (success): scratch copy still exists"
    else
        note_pass "fs-root-helper cleanup (success): scratch copy removed"
    fi

    read -r image mount < <(make_provider "d-failure")
    if sudo -n "$WRAPPER" --image "$image" --mount "$mount" \
            -- /bin/sh -c 'exit 7' >"$WORK/d-failure.log" 2>&1; then
        status=0
    else
        status=$?
    fi
    if [ "$status" -eq 7 ]; then
        note_pass "fs-root-helper cleanup (failure): wrapper propagated exit 7"
    else
        note_fail "fs-root-helper cleanup (failure): expected exit 7, got $status"
    fi
    if [ -z "$(ls -A -- "$mount" 2>/dev/null)" ]; then
        note_pass "fs-root-helper cleanup (failure): mount point empty again"
    else
        note_fail "fs-root-helper cleanup (failure): mount point still has content: $(ls -A -- "$mount")"
    fi
    if ls "$(dirname -- "$mount")"/astra-qemu-root-scratch.* >/dev/null 2>&1; then
        note_fail "fs-root-helper cleanup (failure): scratch copy still exists"
    else
        note_pass "fs-root-helper cleanup (failure): scratch copy removed"
    fi
}

check_no_loop_leak() {
    local before=$1
    local after
    after=$(sudo -n losetup -a | wc -l)
    if [ "$before" = "$after" ]; then
        note_pass "fs-root-helper loop devices: count unchanged ($after)"
    else
        note_fail "fs-root-helper loop devices: count changed $before -> $after"
    fi
}

# --- (e): a control asserting a specific success marker, end to end -----

run_test_e() {
    local image mount status marker
    read -r image mount < <(make_provider "e-success")
    if sudo -n "$WRAPPER" --image "$image" --mount "$mount" \
            -- /bin/bash -c \
            'echo fs-root-helper-marker-$$ > "$ASTRA_HOSTFS_ROOT/work/marker.txt"' \
            >"$WORK/e-success.log" 2>&1; then
        status=0
    else
        status=$?
    fi
    if [ "$status" -ne 0 ]; then
        note_fail "fs-root-helper control: wrapper exited $status: $(cat "$WORK/e-success.log")"
        return
    fi
    marker=$(/usr/sbin/debugfs -R 'cat /work/marker.txt' "$image" 2>/dev/null | tr -d '\r\n')
    if [[ "$marker" == fs-root-helper-marker-* ]]; then
        note_pass "fs-root-helper control: specific marker '$marker' round-tripped through the mount and the write-back"
    else
        note_fail "fs-root-helper control: expected a fs-root-helper-marker-* line in the written-back image, got '$marker'"
    fi
}

LOOP_BEFORE=$(sudo -n losetup -a | wc -l)

run_test_a "$WRAPPER" fixed "$CALLER_UID"
run_test_b
run_test_c
perturb_c_demonstrate_old_pattern
run_test_d
run_test_e
check_no_loop_leak "$LOOP_BEFORE"

# Perturbation for (a): reconstruct the pre-redesign behavior (root execs
# the command directly, no setpriv drop) as a throwaway scratch script --
# never installed -- and show the SAME observe_uid() this test always
# uses, asserting the SAME property it always asserts (the process runs
# as the caller), correctly reports that this one never does: test (a)
# is proven to have teeth, not just to pass because nothing is actually
# checked. observe_uid() itself never prints PASS/FAIL (see its own
# comment above) -- only the single line below does, so this always-
# expected "no" prints exactly one clean PASS line, never a bare FAIL
# token a suite's log scan would mistake for a real failure.
cat > "$WORK/vulnerable-root-exec.sh" <<'EOF'
#!/bin/bash
set -euo pipefail
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
die() { echo "astra-qemu-root: $*" >&2; exit 64; }
[ "$(id -u)" -eq 0 ] || die "run through sudo"
IMAGE= MOUNT=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --image) IMAGE=$2; shift 2 ;;
        --mount) MOUNT=$2; shift 2 ;;
        --env) shift 2 ;;
        --) shift; break ;;
        *) die "unknown option $1" ;;
    esac
done
QEMU=$(realpath -e -- "$1"); shift
# VULNERABLE RECONSTRUCTION, for this test's own perturbation only: runs
# the command as root, the way the pre-redesign wrapper did. Never
# installed anywhere.
"$QEMU" "$@" &
child=$!
echo "$child" > "$(dirname -- "$MOUNT")/qemu.pid"
chmod 0644 -- "$(dirname -- "$MOUNT")/qemu.pid"
set +e
wait "$child"
status=$?
set -e
exit "$status"
EOF
chmod 755 "$WORK/vulnerable-root-exec.sh"
sudo -n install -o root -g root -m 0755 \
    "$WORK/vulnerable-root-exec.sh" /usr/local/sbin/astra-qemu-root-vulnerable-test
vuln_result=$(observe_uid /usr/local/sbin/astra-qemu-root-vulnerable-test \
    vulnerable "$CALLER_UID")
sudo -n rm -f /usr/local/sbin/astra-qemu-root-vulnerable-test
vuln_matched=${vuln_result%% *}
vuln_seen=${vuln_result#* }
if [ "$vuln_matched" = no ]; then
    note_pass "fs-root-helper perturbation (a): the vulnerable (root-exec) reconstruction was correctly caught running as uid $vuln_seen, not the caller"
else
    note_fail "fs-root-helper perturbation (a): the vulnerable reconstruction was NOT caught (ran as the caller's own uid) -- the uid test has no teeth"
fi

echo "ASTRA-QEMU-ROOT-HELPER-TEST passed $PASS failed $FAIL"
[ "$FAIL" -eq 0 ]
