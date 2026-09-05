#!/usr/bin/env python3
"""Check that the Arty launcher owns one QEMU and no fixed evdev fd."""

import hashlib
import os
import pathlib
import shutil
import subprocess
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


def fixture(directory, delay, output="", status=0):
    container = pathlib.Path(directory)
    root = container / "release"
    observed = container / "observed"
    root.mkdir()
    observed.mkdir()
    (root / "qemu/bin").mkdir(parents=True)
    (root / "bin").mkdir()
    (root / "rom").mkdir()
    (root / "rom/astra_boot.bin").write_bytes(b"rom")
    (root / "storage-terminal.img").write_bytes(b"disk")
    shutil.copyfile(RELEASE_TOOL, root / "bin/astra-release.py")
    write_executable(root / "bin/astra-terminal-display", """#!/bin/sh
trap '' TERM
while :; do sleep 1; done
""")
    write_executable(root / "qemu/bin/qemu-system-m68k-astra", f"""#!/bin/sh
test -d "$ASTRA_HOSTFS_ROOT" || exit 97
printf '%s\n' "$ASTRA_HOSTFS_ROOT" >"{observed}/hostfs.root"
printf '%s\\n' "$@" >"{observed}/qemu.args"
sleep {delay}
printf '%s' {output!r}
exit {status}
""")
    write_executable(root / "bin/astra-input-hotplug.py", f"""#!/usr/bin/env python3
import pathlib, signal, sys, time
pathlib.Path({str(observed / 'hotplug.args')!r}).write_text('\\n'.join(sys.argv[1:]))
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
while True: time.sleep(0.05)
""")
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
    return dict(
        os.environ,
        ASTRA_ROOT=str(root),
        ASTRA_STORE=str(observed / "store"),
        ASTRA_STATE_ROOT=str(observed / "state"),
        ASTRA_RUN_ROOT=str(observed / "run"),
        ASTRA_LOG_ROOT=str(observed / "log"),
        ASTRA_HOSTFS_ROOT=str(observed / "hostfs"),
    )


def main():
    if shutil.which("flock") is None:
        print("Astra Arty launcher tests skipped: flock unavailable")
        return
    with tempfile.TemporaryDirectory() as directory:
        root, observed = fixture(directory, 0.2)
        launch_environment = environment(root, observed)
        result = subprocess.run([str(RUN_ARTY)], env=launch_environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 0, result.stderr
        qemu_args = (observed / "qemu.args").read_text().splitlines()
        assert qemu_args.count("-qmp") == 1
        assert "-no-reboot" not in qemu_args
        assert qemu_args[qemu_args.index("-display") + 1] == "none"
        assert qemu_args[qemu_args.index("-serial") + 1] == "stdio"
        assert "-nographic" not in qemu_args
        assert not any("input-linux" in argument for argument in qemu_args)
        hotplug_args = (observed / "hotplug.args").read_text().splitlines()
        assert hotplug_args == ["--qmp", str(observed / "run/qmp.sock")]
        storage = qemu_args[qemu_args.index("-drive") + 1]
        assert storage == "if=none,format=raw,file=%s" % (
            observed / "state/storage-terminal.img")
        assert (observed / "hostfs.root").read_text().strip() == str(
            observed / "hostfs")
        assert (root / "storage-terminal.img").read_bytes() == b"disk"

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
            first.wait(timeout=2.0)
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
