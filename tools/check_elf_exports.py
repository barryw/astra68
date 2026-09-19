#!/usr/bin/env python3
"""Require an ELF DSO's dynamic symbols to exactly match its ABI map."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

try:
    from tools.check_library_exports import exported_symbols
    from tools.generate_archive_contract import visible_definitions
except ModuleNotFoundError:  # Direct execution from the tools directory.
    from check_library_exports import exported_symbols
    from generate_archive_contract import visible_definitions


def differences(expected: set[str], actual: set[str]) -> tuple[list[str], list[str]]:
    return sorted(expected - actual), sorted(actual - expected)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--readelf", required=True)
    parser.add_argument("version_map", type=Path)
    parser.add_argument("library", type=Path)
    arguments = parser.parse_args()

    result = subprocess.run(
        [arguments.readelf, "--wide", "--dyn-syms", str(arguments.library)],
        check=True,
        capture_output=True,
        text=True,
    )
    actual = visible_definitions(result.stdout)
    expected = set(exported_symbols(arguments.version_map))
    missing, unexpected = differences(expected, actual)
    if missing or unexpected:
        sections = []
        if missing:
            sections.append("missing exports:\n  " + "\n  ".join(missing))
        if unexpected:
            sections.append("unexpected exports:\n  " +
                            "\n  ".join(unexpected))
        raise SystemExit(f"{arguments.library}: ABI mismatch\n" +
                         "\n".join(sections))


if __name__ == "__main__":
    main()
