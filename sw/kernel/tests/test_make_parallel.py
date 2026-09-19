#!/usr/bin/env python3
"""Keep destructive kernel goals from racing other goals."""

from pathlib import Path
import subprocess
import sys
import tempfile


def run_probe(not_parallel: str) -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        makefile = root / "Makefile"
        makefile.write_text(
            not_parallel
            + "all: writer cleaner\n"
            + "writer:\n\ttouch marker\n\tsleep 0.2\n\ttest -e marker\n"
            + "cleaner:\n\tsleep 0.05\n\trm -f marker\n"
        )
        return subprocess.run(
            ["make", "-j2", "all"], cwd=root, capture_output=True, check=False
        ).returncode


def main() -> None:
    text = Path(sys.argv[1]).read_text()
    directive = ".NOTPARALLEL:\n" if "\n.NOTPARALLEL:\n" in text else ""
    assert run_probe(directive) == 0, "parallel-goal serialization is ineffective"
    assert run_probe("") != 0, "negative race probe did not expose the hazard"
    print("kernel make parallel-safety tests passed")


if __name__ == "__main__":
    main()
