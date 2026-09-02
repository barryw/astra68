#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "create-arty-benchmark-release.sh"


def main():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        supervisor = root / "astra_supervisor.elf"
        terminal = root / "terminal.elf"
        release = root / "release"
        supervisor.write_bytes(b"supervisor catalog")
        terminal.write_bytes(b"terminal catalog")
        environment = os.environ.copy()
        environment.update({
            "ASTRA_BENCH_SUPERVISOR_ELF": str(supervisor),
            "ASTRA_BENCH_TERMINAL_ELF": str(terminal),
        })
        environment.pop("PYTHONDONTWRITEBYTECODE", None)
        subprocess.run(["sh", str(SCRIPT), str(release)], check=True,
                       env=environment, stdout=subprocess.PIPE, text=True)
        subprocess.run([
            "python3", "-B", "-c", "import event_catalog, trace_decode",
        ], cwd=release / "emu/qemu", check=True,
                       env=environment, stdout=subprocess.PIPE, text=True)
        for script in ("test-filesystem-stress.py",
                       "test-host-channel-userspace.py", "test-terminal.py"):
            subprocess.run([
                "python3", str(release / "emu/qemu" / script), "--help",
            ], check=True, env=environment, stdout=subprocess.PIPE, text=True)
        files = sorted(path.relative_to(release).as_posix()
                       for path in release.rglob("*") if path.is_file())
        expected = [
            "artifacts.sha256",
            "bin/astra-release.py",
            "emu/qemu/astra-input-hotplug.py",
            "emu/qemu/astra_image.py",
            "emu/qemu/event_catalog.py",
            "emu/qemu/test-filesystem-stress.py",
            "emu/qemu/test-host-channel-userspace.py",
            "emu/qemu/test-terminal.py",
            "emu/qemu/trace_decode.py",
            "sw/kernel/trace.h",
            "sw/userspace/services/terminal/build/m68k/terminal.elf",
            "sw/userspace/supervisor/build/m68k/astra_supervisor.elf",
        ]
        assert files == expected, files
    print("ASTRA ARTY BENCHMARK RELEASE TEST PASS")


if __name__ == "__main__":
    main()
