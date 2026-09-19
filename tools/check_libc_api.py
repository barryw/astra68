#!/usr/bin/env python3
"""Prove that Astra libc implements every function declared by its headers."""

from __future__ import annotations

import argparse
import re
import subprocess
import tempfile
from pathlib import Path

try:
    from tools.generate_archive_contract import (
        definitions,
        explicit_symbols,
        read_symbols,
    )
except ModuleNotFoundError:  # Direct execution from the tools directory.
    from generate_archive_contract import definitions, explicit_symbols, read_symbols


AUX_DECLARATION = re.compile(
    r"/\*\s+([^:]+):\d+:[^*]*\*/\s+extern\b(.*)"
)
FUNCTION_CANDIDATE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
KEYWORDS = {"int", "void"}
STANDARD_HEADERS = {
    "assert.h", "complex.h", "ctype.h", "errno.h", "fenv.h", "float.h",
    "inttypes.h", "iso646.h", "limits.h", "locale.h", "math.h",
    "setjmp.h", "signal.h", "stdalign.h", "stdarg.h", "stdatomic.h",
    "stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h",
    "stdnoreturn.h", "string.h", "tgmath.h", "threads.h", "time.h",
    "uchar.h", "wchar.h", "wctype.h",
}

# Installed compatibility/template headers which are not usable Astra C API
# headers and contain no implementation contract to audit.
NON_API_HEADERS = {
    Path("regdef.h"),
    Path("sys/custom_file.h"),
    Path("utmp.h"),
}


def aux_functions(text: str, include_directory: Path) -> set[str]:
    root = include_directory.resolve()
    result: set[str] = set()
    for line in text.splitlines():
        match = AUX_DECLARATION.search(line)
        if match is None:
            continue
        source = Path(match.group(1)).resolve()
        if source == root or root in source.parents:
            relative = source.relative_to(root)
            name = next((candidate.group(1)
                         for candidate in FUNCTION_CANDIDATE.finditer(
                             match.group(2))
                         if candidate.group(1) not in KEYWORDS), None)
            if name is None:
                continue
            compiler_api = (relative.parts[0] == "ssp" or
                            relative.name == "assert.h")
            if name not in KEYWORDS and (not name.startswith("_") or
                                         compiler_api):
                result.add(name)
    return result


def public_headers(include_directory: Path, standard_only: bool = False) -> list[Path]:
    if standard_only:
        return sorted(Path(name) for name in STANDARD_HEADERS
                      if (include_directory / name).is_file())
    headers = []
    for header in include_directory.rglob("*.h"):
        relative = header.relative_to(include_directory)
        if relative in NON_API_HEADERS:
            continue
        if any(part.startswith("_") for part in relative.parts):
            continue
        if relative.parts[0] in {"bits", "c++", "machine"}:
            continue
        headers.append(relative)
    return sorted(headers)


def declared_functions(cc: str, include_directory: Path,
                       standard_only: bool = False) -> set[str]:
    declarations: set[str] = set()
    with tempfile.TemporaryDirectory(prefix="astra-libc-api-") as directory:
        temporary = Path(directory)
        for index, header in enumerate(public_headers(include_directory,
                                                       standard_only)):
            auxiliary = temporary / f"{index}.aux"
            command = [
                cc,
                "-std=c11" if standard_only else "-std=gnu11",
                *( [] if standard_only else ["-D_GNU_SOURCE"] ),
                "-fsyntax-only",
                "-aux-info",
                str(auxiliary),
                "-include",
                "stdarg.h",
                "-include",
                "sys/types.h",
                "-include",
                str(header),
                "-x",
                "c",
                "/dev/null",
            ]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode != 0:
                raise RuntimeError(
                    f"cannot parse installed libc header {header}:\n"
                    f"{result.stderr.strip()}"
                )
            declarations.update(aux_functions(auxiliary.read_text(),
                                              include_directory))
    return declarations


def all_definitions(readelf: str, archive: Path) -> set[str]:
    result = subprocess.run(
        [readelf, "--wide", "--syms", str(archive)],
        check=True,
        capture_output=True,
        text=True,
    )
    return definitions(
        result.stdout,
        {"DEFAULT", "PROTECTED", "HIDDEN", "INTERNAL"},
    )


def write_symbols(path: Path, symbols: set[str]) -> None:
    """Atomically update a sorted symbol manifest without touching it needlessly."""
    content = "".join(f"{name}\n" for name in sorted(symbols))
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(content)
    if path.is_file() and path.read_text() == content:
        temporary.unlink()
    else:
        temporary.replace(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--include-dir", required=True, type=Path)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--extra", required=True, type=Path)
    parser.add_argument("--data", type=Path)
    parser.add_argument("--exclude", type=Path)
    parser.add_argument("--api-out", type=Path)
    parser.add_argument("--missing-out", type=Path)
    parser.add_argument("--require-all", action="store_true")
    parser.add_argument("--standard-only", action="store_true")
    parser.add_argument("--dependency", action="append", default=[], type=Path)
    arguments = parser.parse_args()

    declared = declared_functions(arguments.cc, arguments.include_dir,
                                  arguments.standard_only)
    if arguments.exclude is not None:
        declared.difference_update(explicit_symbols(arguments.exclude))
    implemented = all_definitions(arguments.readelf, arguments.archive)
    provided = explicit_symbols(arguments.extra)
    archive_api = declared & implemented
    if arguments.data is not None:
        data = explicit_symbols(arguments.data)
        unavailable = data - implemented
        if unavailable:
            raise SystemExit(f"{arguments.data}: data symbols not present in "
                             f"archive: {', '.join(sorted(unavailable))}")
        archive_api.update(data)
    if arguments.api_out is not None:
        write_symbols(arguments.api_out, archive_api)

    exported: set[str] = set()
    if arguments.library is not None:
        exported.update(read_symbols(arguments.readelf, arguments.library,
                                     dynamic=True))
    for dependency in arguments.dependency:
        exported.update(read_symbols(arguments.readelf, dependency, dynamic=True))

    absent = declared - implemented - provided
    if arguments.missing_out is not None:
        write_symbols(arguments.missing_out, absent)
    hidden = archive_api - exported if arguments.library is not None else set()
    problems = []
    if arguments.require_all and absent:
        problems.append("declared but not implemented: " +
                        ", ".join(sorted(absent)))
    if hidden:
        problems.append("implemented but not exported: " +
                        ", ".join(sorted(hidden)))
    if problems:
        raise SystemExit("libc API is incomplete:\n  " + "\n  ".join(problems))
    print(f"Astra libc API completeness: PASS ({len(archive_api)} exports, "
          f"{len(absent)} unavailable extensions)")


if __name__ == "__main__":
    main()
