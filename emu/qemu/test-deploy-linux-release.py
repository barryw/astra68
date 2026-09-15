#!/usr/bin/env python3
"""The DE25 deployer must default to the production board and store."""

from pathlib import Path


script = Path(__file__).with_name("deploy-de25-release.sh").read_text()
assert "BOARD=${ASTRA_DE25_BOARD:-root@192.168.1.52}" in script
assert "STORE=${ASTRA_STORE:-/var/lib/astra}" in script
assert "SERVICE=${ASTRA_DE25_SERVICE:-astra.service}" in script
assert "QMP_SOCKET=${ASTRA_DE25_QMP_SOCKET:-/run/astra/qmp.sock}" in script
assert "mkdir -p '$STORE/incoming'" in script
assert "install '$INCOMING' '$STORE'" in script
assert "'$STORE/current/bin/astra-release.py'" in script
assert "'$STORE/current'" in script
assert "systemctl restart '$SERVICE'" in script
assert "expected='$STORE/releases/$IDENTITY/qemu/bin/qemu-system-m68k-astra'" \
    in script
assert "ControlGroup" in script
assert "cgroup.procs" in script
assert r'readlink -f \"/proc/\$process_id/exe\"' in script
assert "[ -S '$QMP_SOCKET' ]" in script
assert "prune '$STORE'" in script
assert script.index("verify --installed") < \
    script.index("systemctl restart '$SERVICE'") < \
    script.index(r'readlink -f \"/proc/\$process_id/exe\"') < \
    script.index("prune '$STORE'")

print("Linux release deployment profile test: PASS")
