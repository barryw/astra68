#!/bin/sh
# Keep a stop job open while a provider still owns kernel resources.
set -eu

IFS=: read -r hierarchy controllers path < /proc/self/cgroup
if [ "$hierarchy" != 0 ] || [ -n "$controllers" ] || [ -z "$path" ]; then
    echo "astra stop guard requires unified cgroup v2" >&2
    exit 1
fi
procs="/sys/fs/cgroup$path/cgroup.procs"
if [ ! -r "$procs" ]; then
    echo "astra stop guard cannot read $procs" >&2
    exit 1
fi

while :; do
    live=0
    while IFS= read -r pid; do
        [ "$pid" = "$$" ] && continue
        [ -d "/proc/$pid" ] || continue
        live=1
        break
    done < "$procs"
    [ "$live" -eq 0 ] && exit 0
    sleep 1
done
