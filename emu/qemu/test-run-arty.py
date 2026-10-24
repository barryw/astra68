#!/usr/bin/env python3
"""Check that the Arty launcher owns one QEMU and no fixed evdev fd."""

import hashlib
import os
import pathlib
import pwd
import shutil
import subprocess
import sys
import tempfile
import time


RUN_ARTY = pathlib.Path(__file__).with_name("run-arty.sh")
RELEASE_TOOL = pathlib.Path(__file__).resolve().parents[2] / \
    "tools/astra_release.py"


def write_executable(path, text):
    path.write_text(text)
    path.chmod(0o755)


def writable(root):
    for path in [root] + list(root.rglob("*")):
        if not path.is_symlink():
            path.chmod(path.stat().st_mode | 0o200)


def fixture(directory, delay, output="", status=0, mailbox_bytes=8193,
            payload_bytes=12345):
    container = pathlib.Path(directory)
    container.chmod(0o755)
    root = container / "release"
    observed = container / "observed"
    root.mkdir()
    observed.mkdir(mode=0o777)
    observed.chmod(0o777)
    (observed / "exports/work").mkdir(parents=True)
    (observed / "exports/sound").mkdir()
    (observed / "bin").mkdir()
    write_executable(observed / "bin/systemctl", f"""#!/bin/sh
printf '%s\\n' "$@" >"{observed}/power.args"
exit "${{ASTRA_TEST_POWER_STATUS:-0}}"
""")
    (root / "qemu/bin").mkdir(parents=True)
    (root / "bin").mkdir()
    write_executable(root / "bin/astra-font-worker", "#!/bin/sh\nexit 0\n")
    (root / "rom").mkdir()
    (root / "rom/astra_boot.bin").write_bytes(b"rom")
    (root / "storage-terminal.img").write_bytes(b"disk")
    shutil.copyfile(RELEASE_TOOL, root / "bin/astra-release.py")
    shutil.copyfile(RUN_ARTY.with_name("runtime-supervisor.py"), root / "bin/runtime-supervisor.py")
    write_executable(root / "bin/astra-terminal-display", f"""#!/bin/sh
if [ "$1" = --mailbox-bytes ]; then echo {mailbox_bytes}; exit 0; fi
if [ "$1" = --payload-bytes ]; then echo {payload_bytes}; exit 0; fi
printf '%s\n' "$@" >"{observed}/display.args"
python3 -c 'import os; print(*sorted(os.sched_getaffinity(0)))' >"{observed}/display.affinity"
count=0
test ! -r "{observed}/display.launches" || read count <"{observed}/display.launches"
count=$((count + 1))
printf '%s\n' "$count" >"{observed}/display.launches"
if [ -e "{observed}/display.exit-once" ]; then
    rm "{observed}/display.exit-once"
    exit 42
fi
trap '' TERM
while :; do sleep 1; done
""")
    write_executable(root / "qemu/bin/qemu-system-m68k-astra", f"""#!/bin/sh
test -n "$ASTRA_STORAGE_CONTROL_FD" && test -n "$ASTRA_STORAGE_PATH_FD" || exit 97
test -z "${{ASTRA_HOSTFS_ROOT+x}}" || exit 98
stat -c %s "$ASTRA_DISPLAY_MAILBOX_PATH" >"{observed}/mailbox.size"
printf '%s\n' "$ASTRA_DISPLAY_PAYLOAD_PATH" >"{observed}/payload.path"
stat -c %s "$ASTRA_DISPLAY_PAYLOAD_PATH" >"{observed}/payload.size"
printf '%s\n' "$ASTRA_HOSTFS_MAX_DESCRIPTORS" "$ASTRA_HOSTFS_MAX_DIRECTORIES" >"{observed}/hostfs.budgets"
printf '%s\\n' "$@" >"{observed}/qemu.args"
id -u >"{observed}/qemu.uid"
id -g >"{observed}/qemu.gid"
sleep {delay}
printf '%s' {output!r}
exit {status}
""")
    # Published selector is read-only data for python3, not an executable.
    (root / "bin/storage_image.py").write_text(
        "#!/usr/bin/env python3\nprint('0 4096 fixture-uuid')\n")
    write_executable(root / "bin/astra-storage", f"""#!/usr/bin/env python3
import os, pathlib, sys
args = sys.argv[1:]
assert os.getuid() == 0
uid, gid = args[args.index('--uid') + 1], args[args.index('--gid') + 1]
assert int(uid) > 0 and int(gid) > 0
pathlib.Path({str(observed / 'provider.args')!r}).write_text('\\n'.join(args))
assert args[args.index('--discovery') + 1] == 'udev'
assert args[args.index('--system') + 4] == 'fixture-uuid'
assert args[args.index('--work') + 1] == {str(observed / 'exports/work')!r}
assert args[args.index('--sound') + 1] == {str(observed / 'exports/sound')!r}
assert args[args.index('--seed-sound') + 1] == {str(root / 'soundfonts')!r}
os.environ['ASTRA_STORAGE_CONTROL_FD'] = '3'
os.environ['ASTRA_STORAGE_PATH_FD'] = '4'
os.execvp('setpriv', ['setpriv', '--reuid', uid, '--regid', gid,
    '--clear-groups', '--no-new-privs', '--bounding-set=-all',
    '--inh-caps=-all', '--ambient-caps=-all', *args[args.index('--') + 1:]])
""")
    write_executable(root / "bin/astra-input-hotplug.py", f"""#!/usr/bin/env python3
import os, pathlib, signal, sys, time
pathlib.Path({str(observed / 'hotplug.args')!r}).write_text('\\n'.join(sys.argv[1:]))
pathlib.Path({str(observed / 'hotplug.affinity')!r}).write_text(
    ' '.join(str(cpu) for cpu in sorted(os.sched_getaffinity(0))))
pathlib.Path({str(observed / 'hotplug.uid')!r}).write_text(str(os.getuid()))
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
while True: time.sleep(0.05)
""")
    (root / "soundfonts").mkdir()
    (root / "soundfonts/Shipped.sf2").write_bytes(b"sfbk")
    (root / "soundfonts/default").write_text("Shipped.sf2\n")
    records = []
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        mode = "x" if path.stat().st_mode & 0o111 else "-"
        records.append("%s  %s  %s\n" %
                       (digest, mode, path.relative_to(root)))
    (root / "artifacts.sha256").write_text("".join(records))
    for path in [item for item in root.rglob("*") if item.is_file()]:
        path.chmod(0o555 if path.stat().st_mode & 0o111 else 0o444)
    for path in reversed([item for item in root.rglob("*") if item.is_dir()]):
        path.chmod(0o555)
    root.chmod(0o555)
    return root, observed


def environment(root, observed):
    available = sorted(os.sched_getaffinity(0))
    return dict(
        os.environ,
        ASTRA_ROOT=str(root),
        ASTRA_STORE=str(observed / "store"),
        ASTRA_STATE_ROOT=str(observed / "state"),
        ASTRA_RUN_ROOT=str(observed / "run"),
        ASTRA_LOG_ROOT=str(observed / "log"),
        ASTRA_EXPORTS_ROOT=str(observed / "exports"),
        ASTRA_QEMU_USER="nobody",
        ASTRA_AUX_CPU=str(available[0]),
        ASTRA_DISPLAY_CPU=str(available[-1]),
        PATH=str(observed / "bin") + os.pathsep + os.environ["PATH"],
    )


def root_privilege_boundary():
    """Root must reject root guests and prepare HostFS as the guest user."""
    assert os.getuid() == 0, "run --root with sudo"
    guest = pwd.getpwnam("nobody")
    with tempfile.TemporaryDirectory() as directory:
        root, observed = fixture(directory, 0.2)
        pathlib.Path(directory).chmod(0o755)
        observed.chmod(0o777)
        hostfs = observed / "exports"
        private = hostfs / "work/private"
        private.write_bytes(b"preserved")
        private.chmod(0o600)
        before = private.stat()
        launch_environment = environment(root, observed)
        launch_environment["ASTRA_QEMU_USER"] = "root"
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode != 0, "root guest was accepted"
        assert not (observed / "qemu.args").exists(), "root guest was launched"
        launch_environment["ASTRA_QEMU_USER"] = "nobody"
        result = subprocess.run([str(RUN_ARTY), "-run-with", "user=0:0"],
                                env=launch_environment, text=True,
                                capture_output=True, check=False)
        assert result.returncode == 1, result.stderr
        assert (observed / "qemu.uid").exists(), (result.stdout, result.stderr)
        assert int((observed / "qemu.uid").read_text()) == guest.pw_uid, \
            "QEMU executable started as root"
        assert int((observed / "qemu.gid").read_text()) == guest.pw_gid
        assert int((observed / "hotplug.uid").read_text()) == guest.pw_uid, \
            "QMP input/affinity helper started as root"
        after = private.stat()
        assert before.st_uid == after.st_uid == 0 and before.st_gid == after.st_gid
        assert before.st_mode == after.st_mode and private.read_bytes() == b"preserved"
        writable(root)
    print("Astra launcher root privilege boundary: PASS")


def main():
    assert os.getuid() == 0, "run launcher fixture as root observer; QEMU peer is nobody"
    root_privilege_boundary()
    launcher = RUN_ARTY.read_text()
    assert "ASTRA_STORE=${ASTRA_STORE:-/var/lib/astra}" in launcher
    assert "ASTRA_STORE=${ASTRA_STORE:-/data/astra}" not in launcher
    if shutil.which("flock") is None:
        print("Astra Arty launcher tests skipped: flock unavailable")
        return
    with tempfile.TemporaryDirectory() as directory:
        # QEMU outlives two of run-arty.sh's 0.5 s supervision polls, so a
        # helper that exits is seen and restarted while it runs.
        root, observed = fixture(directory, 1.2)
        launch_environment = environment(root, observed)
        available = sorted(os.sched_getaffinity(0))
        aux_cpu = available[0]
        display_cpu = available[-1]
        invalid_environment = dict(launch_environment,
                                   ASTRA_AUX_CPU="%d:%d" %
                                   (aux_cpu, display_cpu))
        result = subprocess.run([str(RUN_ARTY)], env=invalid_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode != 0
        assert "helper CPU assignments are invalid" in result.stderr
        private = observed / "exports/work/notes.txt"
        private.write_text("kept")
        private.chmod(0o600)
        before = private.stat()
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 1, result.stderr
        assert private.read_text() == "kept" and private.stat().st_mode == before.st_mode
        assert not (observed / "power.args").exists()
        assert (observed / "qemu.args").exists(), (result.stdout, result.stderr)
        qemu_args = (observed / "qemu.args").read_text().splitlines()
        assert qemu_args.count("-qmp") == 2
        qmp_arguments = [qemu_args[index + 1] for index, value in
                         enumerate(qemu_args) if value == "-qmp"]
        assert qmp_arguments == [
            "unix:%s,server=on,wait=off" % (observed / "run/qemu/qmp.sock"),
            "unix:%s,server=on,wait=off" %
            (observed / "run/qemu/remote-desktop-qmp.sock"),
        ]
        assert "-no-reboot" not in qemu_args
        assert qemu_args[qemu_args.index("-display") + 1] == "none"
        assert qemu_args[qemu_args.index("-serial") + 1] == "stdio"
        assert "-nographic" not in qemu_args
        assert not any("input-linux" in argument for argument in qemu_args)
        hotplug_args = (observed / "hotplug.args").read_text().splitlines()
        assert hotplug_args == ["--qmp", str(observed / "run/qemu/qmp.sock")]
        assert (observed / "display.affinity").read_text().strip() == \
            str(display_cpu)
        assert (observed / "hotplug.affinity").read_text().strip() == \
            str(aux_cpu)
        page_size = os.sysconf("SC_PAGE_SIZE")
        mailbox_size = ((8193 + page_size - 1) // page_size) * page_size
        assert int((observed / "mailbox.size").read_text()) == mailbox_size
        payload = observed / "run/display-payload.bin"
        if not pathlib.Path("/dev/astra-host-arena").exists():
            assert (observed / "payload.path").read_text().strip() == \
                str(payload)
            assert int((observed / "payload.size").read_text()) == 12345
            assert (observed / "display.args").read_text().splitlines() == [
                str(observed / "run/post-text.bin"),
                str(observed / "run/display.bin"), str(payload)]
        assert "-drive" not in qemu_args
        provider_args = (observed / "provider.args").read_text().splitlines()
        assert provider_args[provider_args.index("--system") + 1] == str(observed / "state/storage-terminal.img")
        assert provider_args[provider_args.index("--seed-sound") + 1] == str(root / "soundfonts")
        assert (observed / "hostfs.budgets").read_text().splitlines() == ["2048", "1024"]
        assert (root / "storage-terminal.img").read_bytes() == b"disk"

        default_state = dict(launch_environment)
        default_state.pop("ASTRA_STATE_ROOT")
        result = subprocess.run([str(RUN_ARTY)], env=default_state,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 1, result.stderr
        provider_args = (observed / "provider.args").read_text().splitlines()
        assert provider_args[provider_args.index("--system") + 1] == \
            str(observed / "store/state/system/storage-terminal.img")

        (observed / "display.exit-once").touch()
        launch_environment.update(ASTRA_HOSTFS_MAX_DESCRIPTORS="512",
                                  ASTRA_HOSTFS_MAX_DIRECTORIES="128")
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 1, result.stderr
        assert not (observed / "power.args").exists()
        assert int((observed / "display.launches").read_text()) >= 3
        assert "display helper exited with status 42; restarting" in \
            result.stderr
        assert (observed / "hostfs.budgets").read_text().splitlines() == ["512", "128"]

        (observed / "qemu.args").unlink()
        launch_environment["ASTRA_HOST_TIME_MIN"] = str(int(time.time()) + 60)
        waiting = subprocess.Popen([str(RUN_ARTY)], env=launch_environment,
                                   stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        try:
            time.sleep(0.1)
            assert waiting.poll() is None
            assert not (observed / "qemu.args").exists()
        finally:
            waiting.terminate()
            waiting.wait(timeout=2.0)
        launch_environment.pop("ASTRA_HOST_TIME_MIN")

        (root / "storage-terminal.img").chmod(0o644)
        (root / "storage-terminal.img").write_bytes(b"stale")
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode != 0
        assert "release verification failed" in result.stderr
        writable(root)

    with tempfile.TemporaryDirectory() as directory:
        root, observed = fixture(directory, 1.0)
        launch_environment = environment(root, observed)
        first = subprocess.Popen([str(RUN_ARTY)], env=launch_environment,
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 2.0
            while not (observed / "qemu.args").exists():
                assert time.monotonic() < deadline
                time.sleep(0.01)
            second = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                    text=True, capture_output=True,
                                    check=False)
            assert second.returncode != 0
            assert "already active" in second.stderr
        finally:
            first.terminate()
            # Four helpers each have a bounded one-second stop interval.
            first.wait(timeout=5.0)
            writable(root)
    with tempfile.TemporaryDirectory() as directory:
        root, observed = fixture(directory, 0, status=88)
        launch_environment = environment(root, observed)
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 0, result.stderr
        assert (observed / "power.args").read_text().splitlines() == [
            "poweroff", "--no-block"]
        (observed / "power.args").unlink()
        launch_environment["ASTRA_TEST_POWER_STATUS"] = "1"
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 1, result.stderr
        assert (observed / "power.args").exists()
        writable(root)
    with tempfile.TemporaryDirectory() as directory:
        root, observed = fixture(directory, 0, status=89)
        launch_environment = environment(root, observed)
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 0, result.stderr
        assert (observed / "power.args").read_text().splitlines() == [
            "reboot", "--no-block"]
        (observed / "power.args").unlink()
        launch_environment["ASTRA_TEST_POWER_STATUS"] = "1"
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 1, result.stderr
        assert (observed / "power.args").read_text().splitlines() == [
            "reboot", "--no-block"]
        writable(root)
    with tempfile.TemporaryDirectory() as directory:
        root, observed = fixture(directory, 0, "ASTRA68-QEMU SHUTDOWN\\n",
                                 status=1)
        result = subprocess.run([str(RUN_ARTY)],
                                env=environment(root, observed),
                                text=True, capture_output=True, check=False)
        assert result.returncode == 1, result.stderr
        assert not (observed / "power.args").exists()
        writable(root)
    with tempfile.TemporaryDirectory() as directory:
        root, observed = fixture(directory, 0, "*** AXIOM KERNEL PANIC ***\n"
                                 "Fault:  0x40A00024\nSYSTEM HALTED\n", 1)
        launch_environment = environment(root, observed)
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 1
        report = (observed / "log/panic-latest.log").read_text()
        assert "AXIOM KERNEL PANIC" in report
        assert "Fault:  0x40A00024" in report
        assert "SYSTEM HALTED" in report
        writable(root)
    print("Astra Arty launcher tests passed")


if __name__ == "__main__":
    main()
