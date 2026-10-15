#!/usr/bin/env python3
"""Run the Linux mount seam in its own private namespace on disposable images."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--provider", action="store_true")
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    with tempfile.TemporaryDirectory(prefix="astra-storage-mount-") as work:
        seed = Path(work) / "seed"
        (seed / "Folder").mkdir(parents=True)
        (seed / "Folder" / "café.txt").write_text("Astra ISO test\n")
        for name, format_id, extra in (
                ("rock-ridge.iso", "4", ["-R", "-J"]),
                ("joliet.iso", "4", ["-J"]),
                ("disc.udf", "5", ["-R", "-J", "-udf"])):
            if args.provider and name != "rock-ridge.iso":
                continue
            image = str(Path(work) / name)
            subprocess.run([
                "genisoimage", "-quiet", "-input-charset", "utf-8",
                "-V", "ASTRA_DISC", "-o", image
            ] + extra + [str(seed)], check=True)
            subprocess.run(["sudo", "-n", binary, image] +
                           ([] if args.provider else [format_id]) +
                           [str(os.getuid()), str(os.getgid())], check=True)


if __name__ == "__main__":
    main()
