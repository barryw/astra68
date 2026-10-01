#!/usr/bin/env python3
"""Copy the pinned, unmodified Chocolate Doom tree into build/vendor."""

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

REVISION = "410d96855b5df5410ff591a90efeafa889119224"  # 3.1.1
PORT = Path(__file__).resolve().parent


def prepare(source: Path, output: Path) -> Path:
    source = source.resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        raise ValueError(f"chocolate-doom {revision} != pinned {REVISION}")
    if subprocess.check_output(
            ["git", "-C", str(source), "status", "--porcelain"],
            text=True).strip():
        raise ValueError("chocolate-doom source tree is modified")
    stamp = hashlib.sha256(
        (revision + Path(__file__).read_text()).encode()).hexdigest()
    marker = output / ".astra-source-stamp"
    if marker.exists() and marker.read_text() == stamp:
        return output
    if output.exists():
        shutil.rmtree(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, output, ignore=shutil.ignore_patterns(".git"))
    marker.write_text(stamp)
    return output


if __name__ == "__main__":
    try:
        print(prepare(Path(sys.argv[1]), Path(sys.argv[2])))
    except (IndexError, OSError, subprocess.CalledProcessError,
            ValueError) as error:
        raise SystemExit(f"chocolate-doom preparation failed: {error}")
