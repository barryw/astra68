#!/usr/bin/env python3
"""Validate one Astra dynamically linked executable and its direct ABI set."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path

from check_dynamic_relocations import DynamicRelocationError
from check_dynamic_relocations import violations as relocation_violations


def interpreter(program_headers: str, strings: str) -> str | None:
    if not re.search(r"\bINTERP\b", program_headers):
        return None
    match = re.search(r"\[\s*0*0\]\s+([^\s]+)", strings)
    return match.group(1) if match else None


def needed(dynamic: str) -> list[str]:
    return re.findall(r"\(NEEDED\).*?\[([^]]+)\]", dynamic)


def violations(header: str, program_headers: str, strings: str,
               dynamic: str, expected_interpreter: str,
               expected_needed: list[str]) -> list[str]:
    problems: list[str] = []
    if not re.search(r"Type:\s+EXEC\b", header):
        problems.append("ELF type is not EXEC")
    actual_interpreter = interpreter(program_headers, strings)
    if actual_interpreter != expected_interpreter:
        problems.append(
            f"interpreter is {actual_interpreter!r}, expected "
            f"{expected_interpreter!r}")
    actual_needed = needed(dynamic)
    if actual_needed != expected_needed:
        problems.append(
            f"direct dependencies are {actual_needed!r}, expected "
            f"{expected_needed!r}")
    if "BIND_NOW" not in dynamic:
        problems.append("immediate binding (BIND_NOW) is missing")
    if "TEXTREL" in dynamic:
        problems.append("text relocations are present")
    if not re.search(r"\bGNU_RELRO\b", program_headers):
        problems.append("GNU_RELRO is missing")
    return problems


def read(readelf: str, *arguments: str) -> str:
    return subprocess.run([readelf, *arguments], check=True,
                          capture_output=True, text=True).stdout


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--interpreter", default="loader.library.1")
    parser.add_argument("--needed", action="append", default=[])
    parser.add_argument("executable", type=Path)
    arguments = parser.parse_args()

    header = read(arguments.readelf, "-hW", str(arguments.executable))
    program_headers = read(arguments.readelf, "-lW", str(arguments.executable))
    strings = read(arguments.readelf, "-p", ".interp",
                   str(arguments.executable))
    dynamic = read(arguments.readelf, "-dW", str(arguments.executable))
    problems = violations(header, program_headers, strings, dynamic,
                          arguments.interpreter, arguments.needed)
    try:
        problems.extend(relocation_violations(arguments.executable))
    except DynamicRelocationError as error:
        problems.append(str(error))
    if problems:
        raise SystemExit(f"{arguments.executable}: invalid Astra executable:\n  "
                         + "\n  ".join(problems))


if __name__ == "__main__":
    main()
