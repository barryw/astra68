#!/usr/bin/env bash
set -euo pipefail

bundle=$(cd "$(dirname "$0")" && pwd)
quartus_root=${QUARTUS_ROOT:-/home/barry/altera_pro/26.1.1/quartus}
jtagconfig="$quartus_root/bin/jtagconfig"
quartus_pgm="$quartus_root/bin/quartus_pgm"

(cd "$bundle" && sha256sum -c SHA256SUMS)
test -x "$jtagconfig"
test -x "$quartus_pgm"
test -s "$bundle/astra68.hps.jic"

chain=$($jtagconfig --enum)
mapfile -t cables < <(awk '/^[0-9]+\) DE25-Nano / {
    sub(/^[0-9]+\) /, ""); print
}' <<<"$chain")
if [[ ${#cables[@]} -ne 1 ]]; then
    echo "expected one DE25-Nano JTAG cable, found ${#cables[@]}" >&2
    exit 1
fi
mapfile -t ids < <(awk '
    /^[0-9]+\) DE25-Nano / { selected = 1; next }
    /^[0-9]+\) / { selected = 0 }
    selected && $1 ~ /^[[:xdigit:]]{8}$/ { print $1 }
' <<<"$chain")
if [[ ${ids[*]} != "4BA06477 4362C0DD" ]]; then
    echo "refusing unexpected DE25-Nano JTAG chain: ${ids[*]}" >&2
    exit 1
fi

"$quartus_pgm" -c "${cables[0]}" -m JTAG \
    -o "pvbi;$bundle/astra68.hps.jic;A5EB013BB23BE4SCS@2"
