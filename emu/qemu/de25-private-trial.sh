#!/bin/bash
# Run a verified release on a disposable image, then restore the selected release.
set -Eeuo pipefail

if [ "$#" -ne 4 ] || [ "$(id -u)" -ne 0 ]; then
    echo "usage: de25-private-trial.sh RELEASE IMAGE TRIAL_ROOT SECONDS (root on DE25)" >&2
    exit 2
fi
release=$(readlink -f -- "$1")
image=$(readlink -f -- "$2")
trial_root=$(readlink -f -- "$3")
seconds=$4
for path in "$release" "$image" "$trial_root"; do
    case "$path" in
        *[!A-Za-z0-9_./-]*|*..*) echo "unsafe trial path" >&2; exit 2 ;;
    esac
done
case "$trial_root" in /var/tmp/astra-*) ;; *) echo "trial root must be under /var/tmp/astra-*" >&2; exit 2 ;; esac
case "$release" in "$trial_root"/*) ;; *) echo "release is outside trial root" >&2; exit 2 ;; esac
case "$image" in "$trial_root"/*) ;; *) echo "image is outside trial root" >&2; exit 2 ;; esac
case "$seconds" in ''|*[!0-9]*) echo "invalid trial duration" >&2; exit 2 ;; esac
if [ "$seconds" -lt 1 ] || [ "$seconds" -gt 600 ] ||
   [ ! -f "$image" ] || [ -L "$image" ] ||
   [ ! -x "$release/bin/run-arty.sh" ]; then
    echo "invalid trial inputs" >&2
    exit 2
fi

dropin=/run/systemd/system/astra.service.d/99-astra-private-trial.conf
[ ! -e "$dropin" ] || { echo "private trial already configured" >&2; exit 2; }
selected=$(readlink -f /var/lib/astra/current)
python3 "$release/bin/astra-release.py" verify --installed "$release" >/dev/null
runuser -u astra -- test -r "$release/bin/astra-input-hotplug.py" || {
    echo "candidate release is not readable by Astra helpers" >&2
    exit 1
}
runuser -u astra -- test -x "$release/bin/astra-codec-worker" || {
    echo "candidate Codec worker is not executable by Astra" >&2
    exit 1
}
for unit in astra.service astra-audio-host.service astra-remote-desktop.service; do
    systemctl is-active --quiet "$unit" || { echo "$unit is not active" >&2; exit 1; }
done
install -d -m 0755 "$trial_root/hostfs/work" "$trial_root/hostfs/sound" \
    "$trial_root/state/system" "$trial_root/log"
chmod 0711 "$trial_root"

configured=0
cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ "$configured" -eq 1 ]; then
        systemctl stop astra.service astra-audio-host.service astra-remote-desktop.service || result=1
        rm -f -- "$dropin"
        systemctl daemon-reload || result=1
        systemctl start astra-audio-host.service astra-remote-desktop.service astra.service || result=1
    fi
    if [ "$(readlink -f /var/lib/astra/current)" != "$selected" ]; then
        echo "production release selector changed" >&2
        result=1
    fi
    for unit in astra.service astra-audio-host.service astra-remote-desktop.service; do
        systemctl is-active --quiet "$unit" || { echo "$unit did not recover" >&2; result=1; }
    done
    echo "ASTRA_PRIVATE_TRIAL restored=$selected result=$result"
    exit "$result"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

install -d -m 0755 "$(dirname "$dropin")"
cat >"$dropin" <<EOF
[Service]
Environment=ASTRA_ROOT=$release
Environment=ASTRA_STORE=$trial_root
Environment=ASTRA_STATE_ROOT=$trial_root/state/system
Environment=ASTRA_EXPORTS_ROOT=$trial_root/hostfs
Environment=ASTRA_LOG_ROOT=$trial_root/log
Environment=ASTRA_RUN_ROOT=/run/astra
Environment=STORAGE=$image
ExecStart=
ExecStart=$release/bin/run-arty.sh
Restart=no
EOF
configured=1
systemctl daemon-reload
systemctl stop astra.service astra-audio-host.service astra-remote-desktop.service
systemctl start astra-audio-host.service astra-remote-desktop.service astra.service
for ((elapsed = 0; elapsed < seconds; ++elapsed)); do
    sleep 1
    systemctl is-active --quiet astra.service || {
        echo "candidate runtime exited at ${elapsed}s" >&2
        exit 1
    }
done
echo "ASTRA_PRIVATE_TRIAL ran=${seconds}s root=$trial_root"
