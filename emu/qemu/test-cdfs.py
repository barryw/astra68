#!/usr/bin/env python3
"""Mount a generated optical image through the native CDFS service in QEMU."""

import argparse
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("terminal_gate", HERE / "test-terminal.py")
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("--image", required=True)
    parser.add_argument("--boot-deadline", type=float, default=120.0)
    parser.add_argument("--command-deadline", type=float, default=60.0)
    args = parser.parse_args()
    vfs = HERE.parent.parent / "sw/userspace/vfs"
    subprocess.run(["make", "-C", str(vfs), "cdfs-test"], check=True)
    if not gate.refresh_workspace_rom(args.rom):
        return 1
    with tempfile.TemporaryDirectory(prefix="astra-cdfs-") as directory:
        root = Path(directory)
        image = root / "card.img"
        optical = root / "disc.iso"
        shutil.copyfile(args.image, image)
        subprocess.run([str(vfs / "build/host/test_vfs_cdfs_backend"),
                        "--write-iso", str(optical)], check=True)
        config = gate.astra_image.CONFIGURATION
        key = "services/cdfs/service.conf"
        original = config[key]
        try:
            config[key] = original.replace("enabled false", "enabled true")
            gate.astra_image.install(str(image))
        finally:
            config[key] = original
        gate.astra_image._replace_volume_file(str(image), str(optical), "/home/disc.iso")
        machine = gate.Machine(args.qemu, args.rom, str(image), directory)
        try:
            if not gate.open_terminal(machine, args.boot_deadline, args.command_deadline):
                return 1
            cases = (
                ("ls /cd0; print CDFS-LIST-$?", ("DOCS", "HELLO.TXT", "CDFS-LIST-0")),
                ("cat /cd0/docs/note.txt; print CDFS-READ-$?", ("noteCDFS-READ-0",)),
                ("cd /cd0/docs && cat ./note.txt; print CDFS-CWD-$?; cd /home",
                 ("noteCDFS-CWD-0",)),
                ("cat /cd0/absent; print CDFS-MISSING-$?", ("CDFS-MISSING-1",)),
                ("print bad > /cd0/new; print CDFS-WRITE-$?", ("CDFS-WRITE-1",)),
                ("print x >> /home/disc.iso; cat /cd0/docs/note.txt; print CDFS-CHANGED-$?",
                 ("CDFS-CHANGED-1",)),
                ("service stop cdfs; print CDFS-STOP-$?", ("CDFS-STOP-0",)),
                ("cat /cd0/docs/note.txt; print CDFS-STOPPED-$?", ("CDFS-STOPPED-1",)),
            )
            for command, expected in cases:
                before = machine.sequence()
                machine.qmp.type_line(command)
                if gate.wait_for_command(machine, expected, args.command_deadline, before) is None:
                    print("FAIL:", command)
                    for line in machine.said(before)[0][-40:]:
                        print(line)
                    return 1
            print("ASTRA CDFS PASS: listing, reads, cwd, refusals, changed image and clean stop")
            return 0
        finally:
            machine.close()


if __name__ == "__main__":
    raise SystemExit(main())
