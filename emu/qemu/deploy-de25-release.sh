#!/bin/sh
set -eu

MODE=activate
case "${1:-}" in
    --stage-kernel) MODE=stage; shift ;;
    --arm-kernel) MODE=arm; shift ;;
    --rollback-kernel) MODE=rollback; shift ;;
esac
if [ "$#" -ne 1 ]; then
    echo "usage: $0 [--stage-kernel|--arm-kernel|--rollback-kernel] RELEASE_DIRECTORY" >&2
    exit 2
fi
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
RELEASE=$1
BOARD=${ASTRA_DE25_BOARD:-root@192.168.1.52}
STORE=${ASTRA_STORE:-/var/lib/astra}
SERVICE=${ASTRA_DE25_SERVICE:-astra.service}
QMP_SOCKET=${ASTRA_DE25_QMP_SOCKET:-/run/astra/qemu/qmp.sock}
SSH=${SSH:-ssh}
SCP=${SCP:-scp}
RELEASE_TOOL=$REPOSITORY/tools/astra_release.py
case "$STORE" in
    /*) ;;
    *) echo "Astra store must be an absolute path: $STORE" >&2; exit 2 ;;
esac
case "$STORE" in
    *[!A-Za-z0-9_./-]*|*..*)
        echo "Astra store contains unsafe path characters: $STORE" >&2
        exit 2 ;;
esac
case "$SERVICE" in
    ''|*[!A-Za-z0-9_.@-]*)
        echo "Astra service name is invalid: $SERVICE" >&2
        exit 2 ;;
esac
case "$QMP_SOCKET" in
    /*) ;;
    *) echo "Astra QMP socket must be an absolute path: $QMP_SOCKET" >&2; exit 2 ;;
esac
case "$QMP_SOCKET" in
    *[!A-Za-z0-9_./-]*|*..*)
        echo "Astra QMP socket contains unsafe path characters: $QMP_SOCKET" >&2
        exit 2 ;;
esac
IDENTITY=$(PYTHONDONTWRITEBYTECODE=1 \
    python3 "$RELEASE_TOOL" verify "$RELEASE")
INCOMING=$($SSH "$BOARD" \
    "mkdir -p '$STORE/incoming' && mktemp -d '$STORE/incoming/release.XXXXXX'")

cleanup() {
    status=$?
    if [ -n "$INCOMING" ]; then
        $SSH "$BOARD" "rm -rf '$INCOMING'" >/dev/null 2>&1 || true
    fi
    exit "$status"
}
trap cleanup EXIT
kernel_action() {
    kernel_mode=$1
    kernel_candidate=$2
    $SSH "$BOARD" "
set -eu
boot_mount=/mnt/astra-kernel-boot
mounted=0
mkdir -p \"\$boot_mount\"
if mountpoint -q \"\$boot_mount\"; then
    test \"\$(findmnt -n -o SOURCE --mountpoint \"\$boot_mount\")\" = /dev/mmcblk0p1
else
    # Reuse an existing mount so the same FAT cannot be mounted twice.
    if existing=\$(findmnt -rn -o TARGET -S /dev/mmcblk0p1); then
        test -n \"\$existing\"
        boot_mount=\$existing
    else
        mount -t vfat /dev/mmcblk0p1 \"\$boot_mount\"
        mounted=1
    fi
fi
cleanup_kernel_mount() {
    if [ \"\$mounted\" = 1 ]; then umount \"\$boot_mount\"; fi
}
trap cleanup_kernel_mount EXIT HUP INT TERM
python3 '$kernel_candidate/bin/astra-kernel-install.py' '$kernel_mode' \
    '$kernel_candidate/kernel' \"\$boot_mount\" '$STORE/kernel-boot-state.json'
"
}
$SCP -r "$RELEASE/." "$BOARD:$INCOMING/"
if [ "$MODE" != activate ]; then
    # Install without selecting the runtime. Keep the candidate reachable by
    # the normal retention policy while kernel boot acceptance is pending.
    INSTALLED=$($SSH "$BOARD" \
        "PYTHONDONTWRITEBYTECODE=1 python3 '$INCOMING/bin/astra-release.py' \
install --no-activate '$INCOMING' '$STORE'")
    test "$INSTALLED" = "$IDENTITY"
    CANDIDATE=$STORE/releases/$IDENTITY
    $SSH "$BOARD" "PYTHONDONTWRITEBYTECODE=1 python3 \
'$CANDIDATE/bin/astra-release.py' select '$STORE' by-boot/kernel-stage '$IDENTITY'" >/dev/null
    kernel_action "$MODE" "$CANDIDATE"
    INCOMING=
    echo "ASTRA_DE25_KERNEL_$MODE PASS release=$IDENTITY runtime_selector_unchanged=1"
    exit 0
fi
kernel_action verify "$INCOMING"
# Kernel cutover is a complete Image/module transaction performed before runtime
# activation. Refuse incompatible running kernels before changing any selector.
$SSH "$BOARD" "
set -eu
package='$INCOMING/kernel/package'
python3 '$INCOMING/bin/astra-release.py' verify '$INCOMING' >/dev/null
kernel_release=\$(cat \"\$package/metadata/kernel.release\")
test \"\$(uname -r)\" = \"\$kernel_release\" || {
    echo 'Boot the complete packaged kernel before activating this runtime' >&2; exit 1;
}
"
INSTALLED=$($SSH "$BOARD" \
    "PYTHONDONTWRITEBYTECODE=1 python3 '$INCOMING/bin/astra-release.py' \
install '$INCOMING' '$STORE'")
if [ "$INSTALLED" != "$IDENTITY" ]; then
    echo "installed Astra release identity changed" >&2
    exit 1
fi
ACTIVE=$($SSH "$BOARD" \
    "PYTHONDONTWRITEBYTECODE=1 python3 \
'$STORE/current/bin/astra-release.py' verify --installed \
'$STORE/current'")
if [ "$ACTIVE" != "$IDENTITY" ]; then
    echo "active Astra release identity changed" >&2
    exit 1
fi
$SSH "$BOARD" "
set -eu
runtime_unit_source='$STORE/current/systemd/astra.service'
runtime_unit_target=/etc/systemd/system/astra.service
runtime_unit_temporary=/etc/systemd/system/.astra.service.\$\$
trap 'rm -f \"\$runtime_unit_temporary\"' EXIT HUP INT TERM
install -m 0644 \"\$runtime_unit_source\" \"\$runtime_unit_temporary\"
mv -f \"\$runtime_unit_temporary\" \"\$runtime_unit_target\"
trap - EXIT HUP INT TERM
unit_source='$STORE/current/systemd/astra-remote-desktop.service'
unit_target=/etc/systemd/system/astra-remote-desktop.service
unit_temporary=/etc/systemd/system/.astra-remote-desktop.service.\$\$
trap 'rm -f \"\$unit_temporary\"' EXIT HUP INT TERM
install -m 0644 \"\$unit_source\" \"\$unit_temporary\"
mv -f \"\$unit_temporary\" \"\$unit_target\"
trap - EXIT HUP INT TERM
audio_unit_source='$STORE/current/systemd/astra-audio-host.service'
audio_unit_target=/etc/systemd/system/astra-audio-host.service
audio_unit_temporary=/etc/systemd/system/.astra-audio-host.service.\$\$
trap 'rm -f \"\$audio_unit_temporary\"' EXIT HUP INT TERM
install -m 0644 \"\$audio_unit_source\" \"\$audio_unit_temporary\"
mv -f \"\$audio_unit_temporary\" \"\$audio_unit_target\"
trap - EXIT HUP INT TERM
systemctl daemon-reload
systemctl reenable astra-remote-desktop.service
systemctl reenable astra-audio-host.service
systemctl stop astra-remote-desktop.service
systemctl stop '$SERVICE'
if ! id -u astra >/dev/null 2>&1; then
    useradd --system --user-group --home-dir /nonexistent \
        --shell /usr/sbin/nologin astra
fi
test \"\$(id -u astra)\" -gt 0
test \"\$(id -g astra)\" -gt 0
test ! -L '$STORE/hostfs'
mkdir -p '$STORE/hostfs'
# The root provider preserves existing guest ownership and modes. Private bind
# exports lend exact descriptors to QEMU; no recursive ownership migration.
test ! -L '$STORE/hostfs/work'
test ! -L '$STORE/hostfs/sound'
mkdir -p '$STORE/hostfs/work' '$STORE/hostfs/sound'
install -m 0644 '$STORE/current/udev/99-astra-qemu.rules' \
    /etc/udev/rules.d/99-astra-qemu.rules
udevadm control --reload-rules
modprobe astra_graphics_arena
udevadm trigger --subsystem-match=misc
udevadm trigger --subsystem-match=input
udevadm settle
test -c /dev/astra-front-panel
"
$SSH "$BOARD" "systemctl restart '$SERVICE'"
LIVE=$($SSH "$BOARD" "
set -eu
expected='$STORE/releases/$IDENTITY/qemu/bin/qemu-system-m68k-astra'
guest_uid=\$(id -u astra)
attempt=0
while systemctl is-active --quiet '$SERVICE' && [ \"\$attempt\" -lt 1200 ]; do
    control_group=\$(systemctl show --property ControlGroup --value '$SERVICE')
    case \"\$control_group\" in
        /*)
            processes=/sys/fs/cgroup\$control_group/cgroup.procs
            if [ -r \"\$processes\" ]; then
                for process_id in \$(cat \"\$processes\"); do
                    executable=\$(readlink -f \"/proc/\$process_id/exe\" 2>/dev/null || true)
                    credentials=\$(awk '\$1 == \"Uid:\" {print \$2, \$3, \$4, \$5}' \
                        \"/proc/\$process_id/status\" 2>/dev/null || true)
                    if [ \"\$executable\" = \"\$expected\" ] && [ -S '$QMP_SOCKET' ] &&
                        [ \"\$credentials\" = \"\$guest_uid \$guest_uid \$guest_uid \$guest_uid\" ]; then
                        printf '%s\n' '$IDENTITY'
                        exit 0
                    fi
                done
            fi ;;
        *) ;;
    esac
    attempt=\$((attempt + 1))
    sleep 0.1
done
systemctl status '$SERVICE' --no-pager >&2 || true
exit 1
")
if [ "$LIVE" != "$IDENTITY" ]; then
    echo "running Astra release identity changed" >&2
    exit 1
fi
$SSH "$BOARD" "
set -eu
expected='$STORE/releases/$IDENTITY/bin/astra-audio-host'
test -S /run/astra/audio.sock
systemctl is-active --quiet astra-audio-host.service
process_id=\$(systemctl show --property MainPID --value astra-audio-host.service)
actual=\$(readlink -f \"/proc/\$process_id/exe\" 2>/dev/null || true)
test \"\$actual\" = \"\$expected\"
"
$SSH "$BOARD" "
set -eu
systemctl start astra-remote-desktop.service
expected='$STORE/releases/$IDENTITY/bin/astra-remote-desktop'
attempt=0
while [ \"\$attempt\" -lt 100 ]; do
    if systemctl is-active --quiet astra-remote-desktop.service; then
        process_id=\$(systemctl show --property MainPID --value astra-remote-desktop.service)
        actual=\$(readlink -f \"/proc/\$process_id/exe\" 2>/dev/null || true)
        if [ \"\$actual\" = \"\$expected\" ]; then
            exit 0
        fi
    fi
    attempt=\$((attempt + 1))
    sleep 0.1
done
systemctl status astra-remote-desktop.service --no-pager >&2 || true
echo 'remote desktop did not start from the selected release' >&2
exit 1
"
kernel_action confirm "$STORE/releases/$IDENTITY"
$SSH "$BOARD" "PYTHONDONTWRITEBYTECODE=1 python3 \
'$STORE/current/bin/astra-release.py' prune '$STORE'"
INCOMING=
echo "ASTRA_DE25_RELEASE PASS release=$IDENTITY"
