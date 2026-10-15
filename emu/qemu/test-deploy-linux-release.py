#!/usr/bin/env python3
"""The DE25 deployer must default to the production board and store."""

from pathlib import Path
import json
import os
import subprocess
import tempfile


script = Path(__file__).with_name("deploy-de25-release.sh").read_text()
assert "BOARD=${ASTRA_DE25_BOARD:-root@192.168.1.52}" in script
assert "STORE=${ASTRA_STORE:-/var/lib/astra}" in script
assert "SERVICE=${ASTRA_DE25_SERVICE:-astra.service}" in script
assert "QMP_SOCKET=${ASTRA_DE25_QMP_SOCKET:-/run/astra/qemu/qmp.sock}" in script
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
assert script.index("Boot the complete packaged kernel") < script.index("install '$INCOMING' '$STORE'")
assert "chown -hR" not in script
assert "module_source=" not in script
assert 'kernel_action verify "$INCOMING"' in script
assert script.index('kernel_action verify "$INCOMING"') < script.index("install '$INCOMING' '$STORE'")
assert "install --no-activate" in script
assert "by-boot/kernel-stage" in script
assert "--stage-kernel" in script and "--arm-kernel" in script
assert "kernel/package" in script
assert "udevadm settle" in script


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

# Exercise shell expansion of the remote preflight. Merely sh -n'ing the outer
# script misses literal backslashes/quotes passed to the remote shell.
with tempfile.TemporaryDirectory() as directory:
    fixture = Path(directory)
    ssh = fixture / "ssh"
    ssh.write_text("#!/usr/bin/env python3\nimport json,sys\nprint(json.dumps(sys.argv[2]))\n")
    ssh.chmod(0o755)
    start = script.index("# Kernel cutover")
    preflight = script[start:script.index("INSTALLED=$(", start)]
    completed = subprocess.run(["sh"], input="SSH=" + str(ssh) +
        "\nBOARD=fixture\nINCOMING=/incoming\n" + preflight,
        capture_output=True, text=True, check=True)
    remote = json.loads(completed.stdout)
    assert 'kernel_release=$(cat "$package/metadata/kernel.release")' in remote, remote
    assert 'test "$(uname -r)" = "$kernel_release"' in remote, remote
    subprocess.run(["sh", "-n"], input=remote, text=True, check=True)
    start = script.index('    $SSH "$BOARD" "\nset -eu\nboot_mount=')
    stage = script[start:script.index('\n"\n}', start) + 2]
    completed = subprocess.run(["sh"], input="SSH=" + str(ssh) +
        "\nBOARD=fixture\nkernel_mode=stage\nkernel_candidate=/candidate\nSTORE=/store\n" + stage,
        capture_output=True, text=True, check=True)
    remote = json.loads(completed.stdout)
    assert 'test "$(findmnt -n -o SOURCE --mountpoint "$boot_mount")"' in remote, remote
    assert "'/candidate/bin/astra-kernel-install.py' 'stage'" in remote, remote
    assert "runtime_selector" not in remote and "systemctl" not in remote
    subprocess.run(["sh", "-n"], input=remote, text=True, check=True)
    # An unmounted boot partition makes findmnt exit 1. The remote set -e
    # shell must still mount it, stage the package, then unmount it.
    stubs = fixture / "bin"
    stubs.mkdir()
    calls = fixture / "calls"
    for name in ("mountpoint", "findmnt", "mount", "umount", "python3"):
        command = stubs / name
        if name in ("mountpoint", "findmnt"):
            command.write_text("#!/bin/sh\nexit 1\n")
        else:
            command.write_text("#!/bin/sh\nprintf '%s\\n' " + name +
                               " >> \"$ASTRA_DEPLOY_TEST_CALLS\"\n")
        command.chmod(0o755)
    environment = dict(os.environ, PATH=str(stubs) + ":" + os.environ["PATH"],
                       ASTRA_DEPLOY_TEST_CALLS=str(calls))
    unmounted = remote.replace("boot_mount=/mnt/astra-kernel-boot",
                               "boot_mount=" + str(fixture / "boot"))
    subprocess.run(["sh"], input=unmounted, text=True, check=True,
                   env=environment)
    assert calls.read_text().splitlines() == ["mount", "python3", "umount"]
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
