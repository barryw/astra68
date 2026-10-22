#!/usr/bin/env python3
"""Exercise probe workers against a disposable real Linux filesystem image."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "emu/qemu"))
from qemu_runtime import make_ext4_image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--qemu-user")
    parser.add_argument("--sysroot")
    args = parser.parse_args()
    if bool(args.qemu_user) != bool(args.sysroot):
        parser.error("--qemu-user and --sysroot must be supplied together")
    with tempfile.TemporaryDirectory(prefix="astra-storage-probe-") as work:
        image = make_ext4_image(work, size_mib=16)
        label = "Astra test"
        subprocess.run(["/usr/sbin/e2label", image, label], check=True)
        command = [] if args.qemu_user is None else [
            args.qemu_user, "-L", args.sysroot
        ]
        cases = [(image, label, "1")]
        for bits, size, format_id in ((12, 4, "7"), (16, 32, "8")):
            fat = str(Path(work) / f"fat{bits}.img")
            with open(fat, "wb") as handle:
                handle.truncate(size * 1024 * 1024)
            subprocess.run(["/usr/sbin/mkfs.vfat", "-F", str(bits), "-n",
                            f"ASTRA_FAT{bits}", fat], check=True,
                           stdout=subprocess.DEVNULL)
            cases.append((fat, f"ASTRA_FAT{bits}", format_id))
        ntfs = str(Path(work) / "ntfs.img")
        with open(ntfs, "wb") as handle:
            handle.truncate(64 * 1024 * 1024)
        subprocess.run(["/usr/sbin/mkfs.ntfs", "-F", "-Q", "-L", "ASTRA_NTFS", ntfs],
                       check=True, stdout=subprocess.DEVNULL)
        cases.append((ntfs, "ASTRA_NTFS", "9"))
        iso_seed = Path(work) / "iso-seed"
        iso_seed.mkdir()
        (iso_seed / "Folder").mkdir()
        (iso_seed / "Folder" / "café.txt").write_text("Astra ISO test\n")
        for udf in (False, True):
            disc = str(Path(work) / ("disc.udf" if udf else "disc.iso"))
            subprocess.run([
                "genisoimage", "-quiet", "-R", "-J", "-input-charset", "utf-8",
                "-V", "ASTRA_DISC", "-o", disc
            ] + (["-udf"] if udf else []) + [str(iso_seed)], check=True)
            cases.append((disc, "ASTRA_DISC", "5" if udf else "4"))
        blank = str(Path(work) / "blank.img")
        with open(blank, "wb") as handle:
            handle.truncate(1024 * 1024)
        cases.append((blank, "", "0"))
        for source, source_label, format_id in cases:
            uuid = subprocess.run([
                "/usr/sbin/blkid", "-p", "-o", "value", "-s", "UUID", source
            ], text=True, capture_output=True)
            if uuid.returncode not in (0, 2):
                raise RuntimeError(uuid.stderr)
            before = os.stat(source)
            subprocess.run(command + [args.binary, source, source_label,
                                      uuid.stdout.strip(), format_id], check=True)
            after = os.stat(source)
            assert (before.st_size, before.st_mtime_ns) == (
                after.st_size, after.st_mtime_ns), "read-only probe mutated source"


if __name__ == "__main__":
    main()
