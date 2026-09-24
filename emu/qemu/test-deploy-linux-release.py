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
assert "'$STORE/current/systemd/astra-remote-desktop.service'" in script
assert "'$STORE/current/systemd/astra.service'" in script
assert "runtime_unit_temporary=/etc/systemd/system/.astra.service" in script
assert "unit_temporary=/etc/systemd/system/.astra-remote-desktop.service" in script
assert "install -m 0644" in script
assert "systemctl daemon-reload" in script
assert "systemctl reenable astra-audio-host.service" in script
assert "systemctl enable astra-audio-host.service" not in script
assert "systemctl restart '$SERVICE'" in script
assert "expected='$STORE/releases/$IDENTITY/qemu/bin/qemu-system-m68k-astra'" \
    in script
assert "ControlGroup" in script
assert "cgroup.procs" in script
assert r'readlink -f \"/proc/\$process_id/exe\"' in script
assert "[ -S '$QMP_SOCKET' ]" in script
assert "prune '$STORE'" in script


def check_remote_desktop_lifecycle(source):
    assert "systemctl enable --now astra-remote-desktop.service" not in source
    assert "systemctl reenable astra-remote-desktop.service" in source
    assert "systemctl is-active --quiet astra-remote-desktop.service" in source
    assert 'while [ \\"\\$attempt\\" -lt 100 ]; do' in source
    assert "expected='$STORE/releases/$IDENTITY/bin/astra-remote-desktop'" \
        in source
    assert source.index("verify --installed") < \
        source.index("systemctl daemon-reload") < \
        source.index("systemctl stop astra-remote-desktop.service") < \
        source.index("systemctl restart '$SERVICE'") < \
        source.index(r'readlink -f \"/proc/\$process_id/exe\"') < \
        source.index("systemctl start astra-remote-desktop.service") < \
        source.index("prune '$STORE'")


check_remote_desktop_lifecycle(script)
for broken in (
    script.replace("systemctl stop astra-remote-desktop.service",
                   "systemctl start astra-remote-desktop.service", 1),
    script.replace("systemctl start astra-remote-desktop.service",
                   "systemctl enable --now astra-remote-desktop.service", 1),
    script.replace('while [ \\"\\$attempt\\" -lt 100 ]; do',
                   'if [ \\"\\$attempt\\" -lt 100 ]; then', 1),
):
    try:
        check_remote_desktop_lifecycle(broken)
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError("remote desktop lifecycle regression went undetected")

print("Linux release deployment profile test: PASS")
