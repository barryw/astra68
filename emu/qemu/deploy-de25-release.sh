#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 RELEASE_DIRECTORY" >&2
    exit 2
fi
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
RELEASE=$1
BOARD=${ASTRA_DE25_BOARD:-root@192.168.1.52}
STORE=${ASTRA_STORE:-/var/lib/astra}
SERVICE=${ASTRA_DE25_SERVICE:-astra.service}
QMP_SOCKET=${ASTRA_DE25_QMP_SOCKET:-/run/astra/qmp.sock}
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
$SCP -r "$RELEASE/." "$BOARD:$INCOMING/"
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
unit_source='$STORE/current/systemd/astra-remote-desktop.service'
unit_target=/etc/systemd/system/astra-remote-desktop.service
unit_temporary=/etc/systemd/system/.astra-remote-desktop.service.\$\$
trap 'rm -f \"\$unit_temporary\"' EXIT HUP INT TERM
install -m 0644 \"\$unit_source\" \"\$unit_temporary\"
mv -f \"\$unit_temporary\" \"\$unit_target\"
trap - EXIT HUP INT TERM
systemctl daemon-reload
"
$SSH "$BOARD" "systemctl restart '$SERVICE'"
LIVE=$($SSH "$BOARD" "
set -eu
expected='$STORE/releases/$IDENTITY/qemu/bin/qemu-system-m68k-astra'
while systemctl is-active --quiet '$SERVICE'; do
    control_group=\$(systemctl show --property ControlGroup --value '$SERVICE')
    case \"\$control_group\" in
        /*)
            processes=/sys/fs/cgroup\$control_group/cgroup.procs
            if [ -r \"\$processes\" ]; then
                for process_id in \$(cat \"\$processes\"); do
                    executable=\$(readlink -f \"/proc/\$process_id/exe\" 2>/dev/null || true)
                    if [ \"\$executable\" = \"\$expected\" ] && [ -S '$QMP_SOCKET' ]; then
                        printf '%s\n' '$IDENTITY'
                        exit 0
                    fi
                done
            fi ;;
        *) ;;
    esac
    sleep 0.1
done
systemctl status '$SERVICE' --no-pager >&2 || true
exit 1
")
if [ "$LIVE" != "$IDENTITY" ]; then
    echo "running Astra release identity changed" >&2
    exit 1
fi
$SSH "$BOARD" "PYTHONDONTWRITEBYTECODE=1 python3 \
'$STORE/current/bin/astra-release.py' prune '$STORE'"
INCOMING=
echo "ASTRA_DE25_RELEASE PASS release=$IDENTITY"
