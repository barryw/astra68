#!/usr/bin/env python3
"""The DE25 deployer must default to the production board and store."""

from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile


script = Path(__file__).with_name("deploy-de25-release.sh").read_text()
unit = Path(__file__).resolve().parents[2] / "fpga/de25/astra.service"
assert "Environment=ASTRA_STATE_ROOT=/var/lib/astra/state/system" in unit.read_text()
assert "BOARD=${ASTRA_DE25_BOARD:-root@192.168.1.52}" in script
assert "STORE=${ASTRA_STORE:-/var/lib/astra}" in script
assert "SERVICE=${ASTRA_DE25_SERVICE:-astra.service}" in script
assert "QMP_SOCKET=${ASTRA_DE25_QMP_SOCKET:-/run/astra/qemu/qmp.sock}" in script
assert "mkdir -p '$STORE/incoming'" in script
assert "install '$INCOMING' '$STORE'" in script
assert "astra-system-image-migrate.py" in script
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
        source.rindex("systemctl daemon-reload") < \
        source.rindex("systemctl stop astra-remote-desktop.service") < \
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
    preflight = script[start:script.index("PREVIOUS_ID=$(", start)]
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
    start = script.index('CUTOVER_STARTED=1\n$SSH "$BOARD" "\nset -eu\nstate_dir=')
    migration = script[start:script.index("\nINSTALLED=$(", start)]
    previous_id = "a" * 64
    store = fixture / "store"
    completed = subprocess.run(["sh"], input="SSH=" + str(ssh) +
        "\nBOARD=fixture\nSTORE=" + str(store) +
        "\nPREVIOUS_ID=" + previous_id +
        "\nINCOMING=/incoming\nROLLBACK_STATE=" +
        str(store / "state/system/rollback.img") +
        "\nSERVICE=astra.service\n" + migration,
        capture_output=True, text=True, check=True)
    remote = json.loads(completed.stdout)
    subprocess.run(["sh", "-n"], input=remote, text=True, check=True)
    if sys.platform.startswith("linux"):
        old = store / "state" / previous_id / "storage-terminal.img"
        old.parent.mkdir(parents=True)
        old.write_bytes(b"user data survives release changes")
        stable = store / "state/system/storage-terminal.img"
        stable.parent.mkdir()
        stable.write_bytes(b"stale trial")
        unit = fixture / "astra.service"
        unit.write_text("[Service]\n")
        systemctl = stubs / "systemctl"
        systemctl.write_text("#!/bin/sh\nexit 0\n")
        systemctl.chmod(0o755)
        command = remote.replace("/etc/systemd/system/astra.service", str(unit))
        subprocess.run(["sh"], input=command, text=True, check=True,
                       env=environment)
        assert stable.read_bytes() == old.read_bytes()
        assert stable.stat().st_mode & 0o777 == 0o600
        unit.write_text("Environment=ASTRA_STATE_ROOT=" +
                        str(stable.parent) + "\n")
        old.write_bytes(b"obsolete release copy")
        subprocess.run(["sh"], input=command, text=True, check=True,
                       env=environment)
        assert stable.read_bytes() == b"user data survives release changes"
        assert (store / "state/system/rollback.img").read_bytes() == \
            b"user data survives release changes"
        assert calls.read_text().splitlines().count("python3") == 3
    # A repeat stage/arm leaves the transfer in incoming when the release is
    # already installed. Successful exit must clean that verified duplicate.
    cleanup_ssh = fixture / "cleanup-ssh"
    cleanup_ssh.write_text("#!/bin/sh\nprintf '%s\\n' \"$2\" > \"$ASTRA_DEPLOY_TEST_CALLS\"\n")
    cleanup_ssh.chmod(0o755)
    cleanup_calls = fixture / "cleanup-calls"
    cleanup = script[script.index("cleanup() {"):script.index("kernel_action() {")]
    assert script.count("\nINCOMING=") == 1
    subprocess.run(["sh"], input="SSH=" + str(cleanup_ssh) +
        "\nBOARD=fixture\nINCOMING=/incoming\n" + cleanup +
        "\ntrap cleanup EXIT\ntrue\n", text=True, check=True,
        env=dict(os.environ, ASTRA_DEPLOY_TEST_CALLS=str(cleanup_calls)))
    assert cleanup_calls.read_text() == "rm -rf '/incoming'\n"
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
