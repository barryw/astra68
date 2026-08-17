#!/usr/bin/env python3
"""Check that the Arty launcher owns one QEMU and no fixed evdev fd."""

import os
import pathlib
import shutil
import subprocess
import tempfile
import time


RUN_ARTY = pathlib.Path(__file__).with_name("run-arty.sh")


def write_executable(path, text):
    path.write_text(text)
    path.chmod(0o755)


def fixture(directory, delay, output="", status=0):
    root = pathlib.Path(directory)
    (root / "qemu/bin").mkdir(parents=True)
    (root / "qemu/lib").mkdir()
    (root / "bin").mkdir()
    (root / "rom").mkdir()
    (root / "run").mkdir()
    (root / "rom/astra_boot.bin").write_bytes(b"rom")
    (root / "storage-terminal.img").write_bytes(b"disk")
    write_executable(root / "bin/astra-terminal-display", """#!/bin/sh
trap '' TERM
while :; do sleep 1; done
""")
    write_executable(root / "qemu/bin/qemu-system-m68k-astra", f"""#!/bin/sh
printf '%s\\n' "$@" >"{root}/qemu.args"
sleep {delay}
printf '%s' {output!r}
exit {status}
""")
    write_executable(root / "bin/astra-input-hotplug.py", f"""#!/usr/bin/env python3
import pathlib, signal, sys, time
pathlib.Path({str(root / 'hotplug.args')!r}).write_text('\\n'.join(sys.argv[1:]))
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
while True: time.sleep(0.05)
""")
    return root


def main():
    if shutil.which("flock") is None:
        print("Astra Arty launcher tests skipped: flock unavailable")
        return
    with tempfile.TemporaryDirectory() as directory:
        root = fixture(directory, 0.2)
        environment = dict(os.environ, ASTRA_ROOT=str(root))
        result = subprocess.run([str(RUN_ARTY)], env=environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 0, result.stderr
        qemu_args = (root / "qemu.args").read_text().splitlines()
        assert qemu_args.count("-qmp") == 1
        assert "-no-reboot" not in qemu_args
        assert qemu_args[qemu_args.index("-display") + 1] == "none"
        assert qemu_args[qemu_args.index("-serial") + 1] == "stdio"
        assert "-nographic" not in qemu_args
        assert not any("input-linux" in argument for argument in qemu_args)
        hotplug_args = (root / "hotplug.args").read_text().splitlines()
        assert hotplug_args == ["--qmp", str(root / "run/qmp.sock")]

    with tempfile.TemporaryDirectory() as directory:
        root = fixture(directory, 1.0)
        environment = dict(os.environ, ASTRA_ROOT=str(root))
        first = subprocess.Popen([str(RUN_ARTY)], env=environment,
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 2.0
            while not (root / "qemu.args").exists():
                assert time.monotonic() < deadline
                time.sleep(0.01)
            second = subprocess.run([str(RUN_ARTY)], env=environment,
                                    text=True, capture_output=True,
                                    check=False)
            assert second.returncode != 0
            assert "already active" in second.stderr
        finally:
            first.terminate()
            first.wait(timeout=2.0)
    with tempfile.TemporaryDirectory() as directory:
        root = fixture(directory, 0, "*** AXIOM KERNEL PANIC ***\n"
                       "Fault:  0x40A00024\nSYSTEM HALTED\n", 1)
        environment = dict(os.environ, ASTRA_ROOT=str(root))
        result = subprocess.run([str(RUN_ARTY)], env=environment,
                                text=True, capture_output=True, check=False)
        assert result.returncode == 1
        report = (root / "log/panic-latest.log").read_text()
        assert "AXIOM KERNEL PANIC" in report
        assert "Fault:  0x40A00024" in report
        assert "SYSTEM HALTED" in report
    print("Astra Arty launcher tests passed")


if __name__ == "__main__":
    main()
