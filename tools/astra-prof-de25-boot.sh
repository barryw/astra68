#!/bin/sh
set -eu

if [ "$#" -ne 18 ]; then
    echo "usage: $0 WORK QEMU PLUGIN ROM STORAGE STOP_MARKER TIMEOUT MEMORY HOST_PERF MODE RUNS WARMUPS COMMAND CLEANUP READY_MESSAGE DESKTOP_READY_MESSAGE PROBES MAX_MILLISECONDS" >&2
    exit 2
fi
work=$1
qemu=$2
plugin=$3
rom=$4
storage=$5
stop_marker=$6
timeout=$7
memory=$8
host_perf=$9
mode=${10}
runs=${11}
warmups=${12}
command=${13}
cleanup=${14}
ready_message=${15}
desktop_ready_message=${16}
probes=${17}
max_milliseconds=${18}
case "$timeout" in ''|*[!0-9]*|0) echo "invalid timeout" >&2; exit 2 ;; esac
case "$memory" in true|false) ;; *) echo "invalid memory mode" >&2; exit 2 ;; esac
case "$host_perf" in true|false) ;; *) echo "invalid host perf mode" >&2; exit 2 ;; esac
case "$mode" in boot|command|latency) ;; *) echo "invalid profile mode" >&2; exit 2 ;; esac
case "$runs:$warmups" in *[!0-9:]*|:*) echo "invalid run count" >&2; exit 2 ;; esac
if [ "$runs" -eq 0 ]; then echo "runs must be positive" >&2; exit 2; fi
if [ -n "$probes" ]; then
    saved_ifs=$IFS
    IFS=,
    set -- $probes
    IFS=$saved_ifs
    for probe_option do
        case "$probe_option" in
            probe=0x*) probe_address=${probe_option#probe=0x} ;;
            register=*)
                probe_register=${probe_option#register=}
                case "$probe_register" in
                    ''|*[!0-9A-Za-z_.-]*)
                        echo "invalid probe register" >&2; exit 2 ;;
                esac
                continue ;;
            *) echo "invalid probe" >&2; exit 2 ;;
        esac
        case "$probe_address" in
            ''|*[!0-9a-fA-F]*) echo "invalid probe address" >&2; exit 2 ;;
        esac
    done
fi
if { [ "$mode" = command ] || [ "$mode" = latency ]; } && \
        { [ -z "$command" ] || [ -z "$ready_message" ] || \
          [ -z "$desktop_ready_message" ]; }; then
    echo "command mode requires a command and ready messages" >&2
    exit 2
fi
for artifact in "$qemu" "$rom" "$storage"; do
    [ -f "$artifact" ] || { echo "missing artifact: $artifact" >&2; exit 2; }
done
if [ "$mode" != latency ] && [ ! -f "$plugin" ]; then
    echo "missing artifact: $plugin" >&2
    exit 2
fi

was_astra=$(systemctl is-active astra.service || true)
was_remote=$(systemctl is-active astra-remote-desktop.service || true)
runner_pid=
restore()
{
    if [ -n "$runner_pid" ]; then
        kill "$runner_pid" 2>/dev/null || true
        wait "$runner_pid" 2>/dev/null || true
        runner_pid=
    fi
    if [ "$was_astra" = active ]; then
        systemctl start astra.service
    fi
    if [ "$was_remote" = active ]; then
        systemctl start astra-remote-desktop.service
    fi
}
trap restore EXIT HUP INT TERM

systemctl stop astra-remote-desktop.service
systemctl stop astra.service
mkdir -p "$work/run" "$work/state" "$work/log" "$work/hostfs"
rm -f "$work/profile.aprof"
qemu_launcher=$qemu
perf=
if [ "$host_perf" = true ]; then
    for candidate in /usr/lib/linux-tools/*/perf /usr/bin/perf; do
        if [ -x "$candidate" ] && "$candidate" version >/dev/null 2>&1; then
            perf=$candidate
            break
        fi
    done
    [ -n "$perf" ] || { echo "working perf not found" >&2; exit 1; }
    qemu_launcher=$work/qemu-perf-wrapper.sh
fi
{
    date -u '+started=%Y-%m-%dT%H:%M:%SZ'
    uname -a
    readlink -f /var/lib/astra/current
    sha256sum "$qemu" "$rom" "$storage"
    if [ "$mode" != latency ]; then sha256sum "$plugin"; fi
} > "$work/metadata.txt"

if [ "$mode" = latency ]; then
    env ASTRA_RUN_ROOT="$work/run" ASTRA_STATE_ROOT="$work/state" \
    ASTRA_LOG_ROOT="$work/log" ASTRA_HOSTFS_ROOT="$work/hostfs" \
    QEMU="$qemu_launcher" ROM="$rom" ASTRA_QEMU_BINARY="$qemu" \
    ASTRA_BASE_STORAGE="$storage" \
    ASTRA_PERF_DATA="$work/host-perf.data" ASTRA_PERF_BINARY="${perf:-perf}" \
    /var/lib/astra/current/bin/run-arty.sh \
    > "$work/runner.log" 2>&1 &
else
    profile_mode=control=$work/profile.sock
    if [ "$mode" = boot ]; then
        profile_mode=autostart=true,label=boot
    fi
    plugin_options=$plugin,output=$work/profile.aprof,$profile_mode,memory=$memory
    if [ -n "$probes" ]; then
        plugin_options=$plugin_options,$probes
    fi
    env ASTRA_RUN_ROOT="$work/run" ASTRA_STATE_ROOT="$work/state" \
        ASTRA_LOG_ROOT="$work/log" ASTRA_HOSTFS_ROOT="$work/hostfs" \
        QEMU="$qemu_launcher" ROM="$rom" ASTRA_QEMU_BINARY="$qemu" \
        ASTRA_BASE_STORAGE="$storage" \
        ASTRA_PERF_DATA="$work/host-perf.data" ASTRA_PERF_BINARY="${perf:-perf}" \
        /var/lib/astra/current/bin/run-arty.sh \
        -plugin "$plugin_options" \
        > "$work/runner.log" 2>&1 &
fi
runner_pid=$!
elapsed=0
result=1
while [ "$elapsed" -lt "$timeout" ]; do
    if grep -Fq "$stop_marker" "$work/log/qemu-console.log" 2>/dev/null; then
        result=0
        break
    fi
    if grep -Fq 'System degraded:' "$work/log/qemu-console.log" 2>/dev/null; then
        echo "Astra degraded before profile stop marker" >&2
        break
    fi
    if ! kill -0 "$runner_pid" 2>/dev/null; then
        echo "Astra exited before profile stop marker" >&2
        break
    fi
    sleep 1
    elapsed=$((elapsed + 1))
done
if [ "$elapsed" -ge "$timeout" ]; then
    echo "Astra profile timed out" >&2
fi
if [ "$result" -eq 0 ] && [ "$mode" = command ]; then
    if [ -n "$cleanup" ]; then
        python3 "$work/measure-terminal-text.py" --qmp "$work/run/qmp.sock" \
            --open-terminal --runs "$runs" --warmups "$warmups" \
            --deadline "$timeout" \
            --profile-control "$work/profile.sock" --profile-label command \
            --ready-message "$ready_message" --trace-ring "$work/live-trace.bin" \
            --desktop-ready-message "$desktop_ready_message" \
            --cleanup "$cleanup" "$command" > "$work/command-metrics.jsonl" || result=$?
    else
        python3 "$work/measure-terminal-text.py" --qmp "$work/run/qmp.sock" \
            --open-terminal --runs "$runs" --warmups "$warmups" \
            --deadline "$timeout" \
            --profile-control "$work/profile.sock" --profile-label command \
            --ready-message "$ready_message" --trace-ring "$work/live-trace.bin" \
            --desktop-ready-message "$desktop_ready_message" \
            "$command" > "$work/command-metrics.jsonl" || result=$?
    fi
fi
if [ "$result" -eq 0 ] && [ "$mode" = latency ]; then
    if [ -n "$cleanup" ]; then
        python3 "$work/measure-terminal-text.py" --qmp "$work/run/qmp.sock" \
            --open-terminal --runs "$runs" --warmups "$warmups" \
            --deadline "$timeout" \
            --ready-message "$ready_message" --trace-ring "$work/live-trace.bin" \
            --desktop-ready-message "$desktop_ready_message" \
            --max-milliseconds "$max_milliseconds" --cleanup "$cleanup" \
            "$command" > "$work/command-metrics.jsonl" || result=$?
    else
        python3 "$work/measure-terminal-text.py" --qmp "$work/run/qmp.sock" \
            --open-terminal --runs "$runs" --warmups "$warmups" \
            --deadline "$timeout" \
            --ready-message "$ready_message" --trace-ring "$work/live-trace.bin" \
            --desktop-ready-message "$desktop_ready_message" \
            --max-milliseconds "$max_milliseconds" \
            "$command" > "$work/command-metrics.jsonl" || result=$?
    fi
fi
python3 "$work/measure-terminal-text.py" --qmp "$work/run/qmp.sock" \
    --dump-trace-ring "$work/trace.bin" || true
if [ "$host_perf" = true ] && [ -S "$work/run/qmp.sock" ]; then
    python3 "$work/measure-terminal-text.py" --qmp "$work/run/qmp.sock" \
        --quit || true
    count=0
    while kill -0 "$runner_pid" 2>/dev/null && [ "$count" -lt 100 ]; do
        state=$(sed -n 's/^State:[[:space:]]*\([^[:space:]]\).*/\1/p' \
            "/proc/$runner_pid/status" 2>/dev/null || true)
        [ "$state" = Z ] && break
        count=$((count + 1))
        sleep 0.1
    done
fi
kill "$runner_pid" 2>/dev/null || true
wait "$runner_pid" 2>/dev/null || true
runner_pid=
cp "$work/log/qemu-console.log" "$work/console.log"
if [ "$mode" != latency ]; then
    [ -s "$work/profile.aprof" ] || { echo "profiler emitted no data" >&2; exit 1; }
fi
if [ "$host_perf" = true ]; then
    [ -s "$work/host-perf.data" ] || { echo "perf emitted no data" >&2; exit 1; }
    "$perf" report --stdio --no-children -i "$work/host-perf.data" \
        > "$work/host-perf.txt"
    grep -Eq '^# Samples: [1-9]' "$work/host-perf.txt" || {
        echo "perf report contained no samples" >&2
        exit 1
    }
fi
exit "$result"
