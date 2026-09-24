#!/bin/sh
# Run an isolated physical audio gate, restoring the installed daemon on exit.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 TEST_DIRECTORY VOICES" >&2
    exit 2
fi
directory=$1
voices=$2
case "$directory" in
    /tmp/astra-audio-*) ;;
    *) echo "test directory must be under /tmp/astra-audio-*" >&2; exit 2 ;;
esac
case "$voices" in
    1|2|16) ;;
    *) echo "voices must be 1, 2, or 16" >&2; exit 2 ;;
esac
test -x "$directory/astra-audio-host"
test -f "$directory/test-astra-audio-stream.py"
test -f "$directory/tone.pcm"

was_active=0
runtime_was_active=0
if systemctl is-active --quiet astra.service; then
    runtime_was_active=1
fi
if systemctl is-active --quiet astra-audio-host.service; then
    was_active=1
    systemctl stop astra-audio-host.service
fi
daemon_pid=
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    if [ -n "$daemon_pid" ]; then
        kill "$daemon_pid" 2>/dev/null || true
        wait "$daemon_pid" 2>/dev/null || true
    fi
    if [ -f "$directory/daemon.log" ]; then
        cat "$directory/daemon.log"
    fi
    if [ "$was_active" -eq 1 ]; then
        systemctl start astra-audio-host.service || status=1
    fi
    if [ "$runtime_was_active" -eq 1 ]; then
        systemctl restart astra.service || status=1
    fi
    exit "$status"
}
trap cleanup EXIT HUP INT TERM

"$directory/astra-audio-host" --socket "$directory/audio.sock" \
    >"$directory/daemon.log" 2>&1 &
daemon_pid=$!
attempt=0
while [ ! -S "$directory/audio.sock" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -eq 100 ]; then
        echo "audio daemon did not create its socket" >&2
        exit 1
    fi
    sleep 0.05
done
python3 "$directory/test-astra-audio-stream.py" \
    --socket "$directory/audio.sock" --pcm "$directory/tone.pcm" \
    --voices "$voices" --owner-binary "$directory/astra-audio-host"
