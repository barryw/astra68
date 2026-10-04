#!/bin/sh
# usage: de25-ab.sh [-n RUNS] [-s SECONDS] [-x X,Y] RELEASE...
#
# The DE25 A/B: for each release (a directory under
# /var/lib/astra/releases, or a unique prefix of one), switch to it,
# restart Astra, open the application at X,Y on the desktop (default 71,100,
# Chocolate Doom), let it settle, then measure three astra-top windows:
# idle, pointer motion at 125 Hz, idle again. RUNS rounds interleave the
# releases (A B A B ...) so drift over time is not mistaken for a
# difference. One JSON object per window on stdout, a summary on stderr.
# The board is left on the last release given.
set -eu

runs=1
seconds=30
launch=71,100
while getopts n:s:x: option; do
    case $option in
        n) runs=$OPTARG ;;
        s) seconds=$OPTARG ;;
        x) launch=$OPTARG ;;
        *) sed -n '2,12p' "$0" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ "$#" -ne 0 ] || { sed -n '2,12p' "$0" >&2; exit 2; }

here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
store=${ASTRA_STORE:-/var/lib/astra}
password=/etc/astra/remote-desktop.password
frame=$(mktemp)
trap 'rm -f "$frame"' EXIT

resolve() {
    set -- "$store"/releases/"$1"*
    [ "$#" -eq 1 ] && [ -d "$1" ] || {
        echo "de25-ab: no single release matches" >&2; exit 2; }
    basename "$1"
}

rfb() {
    python3 "$here/de25-rfb.py" "$frame" --password-file "$password" "$@" \
        >/dev/null 2>&1
}

window() {
    label=$1; shift
    python3 "$here/astra-top" "$seconds" --json "$@" |
        python3 -c 'import json, sys
r = json.load(sys.stdin); r["release"], r["window"] = sys.argv[1:3]
p = r.get("pointer", {})
print(json.dumps(r))
print("%-10s %-8s %5.1f presents/s  gaps %3d  idle %4.1f%%  "
      "switches %5.0f/s (%4.0f cross)%s" % (
          r["release"][:10], r["window"], r.get("presents_per_s", 0),
          r.get("audio", {}).get("software_gaps", -1), r["guest"]["idle"],
          r["guest"]["scheduler_per_s"]["context_switches"],
          r["guest"]["scheduler_per_s"]["cross_space_switches"],
          "  cursor %.1f/s max %.0f ms" % (p["commits_per_s"],
                                          p.get("gap_ms_max", 0))
          if p else ""), file=sys.stderr)' "$release" "$label"
}

x=${launch%,*}
y=${launch#*,}
round=0
while [ "$round" -lt "$runs" ]; do
    round=$((round + 1))
    for wanted in "$@"; do
        release=$(resolve "$wanted")
        ln -sfn "releases/$release" "$store/current"
        systemctl restart astra.service
        ready=0
        for _ in $(seq 1 90); do
            if rfb; then ready=1; break; fi
            sleep 2
        done
        [ "$ready" -eq 1 ] || { echo "de25-ab: $release never came up" >&2; exit 1; }
        sleep 20
        rfb --double-click "$x" "$y"
        sleep 20
        window idle
        window motion --motion 125
        window idle-after
    done
done
