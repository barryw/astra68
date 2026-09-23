#!/usr/bin/env python3
"""Reject target functions whose compiler-reported frame exceeds a budget."""

import argparse
from pathlib import Path


def oversized(paths: list[Path], maximum: int) -> list[str]:
    failures = []
    for path in paths:
        for number, line in enumerate(path.read_text().splitlines(), 1):
            fields = line.split("\t")
            if len(fields) < 2:
                raise ValueError(f"{path}:{number}: malformed stack usage")
            try:
                used = int(fields[1])
            except ValueError as error:
                raise ValueError(
                    f"{path}:{number}: invalid stack usage {fields[1]!r}"
                ) from error
            if used > maximum:
                failures.append(f"{fields[0]}: {used} bytes (max {maximum})")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--max", type=int, required=True, dest="maximum")
    parser.add_argument("files", nargs="+", type=Path)
    arguments = parser.parse_args()
    failures = oversized(arguments.files, arguments.maximum)
    if failures:
        print("kernel stack frame budget exceeded:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
