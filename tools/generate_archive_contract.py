#!/usr/bin/env python3
"""Generate an explicit ELF ABI and archive-selection contract.

Picolibc marks every public declaration with ``__picolibc_export``.  It is
built with hidden visibility by default, so the compiler carries that source
of truth into each object's ELF symbol visibility.  This tool reads that
metadata; it does not guess an ABI from implementation symbols or scrape C
declarations.

The generated undefined-symbol response file makes the static PIC archives
contribute every public implementation to ``libc.library`` while allowing an
Astra implementation already linked from the POSIX layer to win normally.
Symbols supplied by lower-level shared libraries are excluded, preserving one
implementation of primitives such as memcpy across the process.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def definitions(output: str, visibilities: set[str]) -> set[str]:
    """Return definitions with one of the requested ELF visibilities."""
    symbols: set[str] = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].rstrip(":").isdigit():
            continue
        _, _, _, kind, binding, visibility, section, name, *_ = fields
        if (binding not in {"GLOBAL", "WEAK"} or
                visibility not in visibilities or
                section in {"UND", "UNDEF", "ABS"} or
                kind in {"FILE", "SECTION"}):
            continue
        name = name.split("@", 1)[0]
        if IDENTIFIER.fullmatch(name):
            symbols.add(name)
    return symbols


def visible_definitions(output: str) -> set[str]:
    """Return externally visible definitions from GNU readelf symbol output."""
    return definitions(output, {"DEFAULT", "PROTECTED"})


def read_symbols(readelf: str, path: Path, dynamic: bool = False) -> set[str]:
    option = "--dyn-syms" if dynamic else "--syms"
    result = subprocess.run(
        [readelf, "--wide", option, str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return visible_definitions(result.stdout)


def explicit_symbols(path: Path) -> set[str]:
    symbols: set[str] = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(),
                                 start=1):
        value = raw.split("#", 1)[0].strip()
        if not value:
            continue
        if not IDENTIFIER.fullmatch(value):
            raise ValueError(f"{path}:{number}: invalid C identifier: {value}")
        if value in symbols:
            raise ValueError(f"{path}:{number}: duplicate symbol: {value}")
        symbols.add(value)
    return symbols


def omit_symbols(path: Path, archive_symbols: set[str]) -> set[str]:
    """Read intentional ABI omissions and reject stale or misspelled names."""
    omitted = explicit_symbols(path)
    unknown = omitted - archive_symbols
    if unknown:
        names = ", ".join(sorted(unknown))
        raise ValueError(f"{path}: omitted symbols not present in archives: "
                         f"{names}")
    return omitted


def write_version_map(path: Path, version: str, symbols: set[str]) -> None:
    if not IDENTIFIER.fullmatch(version):
        raise ValueError(f"invalid ELF version identifier: {version}")
    if not symbols:
        raise ValueError("shared-library export set is empty")
    lines = [f"{version} {{", "    global:"]
    lines.extend(f"        {symbol};" for symbol in sorted(symbols))
    lines.extend(["    local:", "        *;", "};", ""])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_undefined_response(path: Path, symbols: set[str]) -> None:
    if not symbols:
        raise ValueError("shared-library archive selection set is empty")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(f"--undefined={symbol}\n" for symbol in sorted(symbols)),
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--exports-out", required=True, type=Path)
    parser.add_argument("--undefined-out", required=True, type=Path)
    parser.add_argument("--extra", required=True, type=Path)
    parser.add_argument(
        "--force-extra", action="store_true",
        help="also force explicitly listed exports out of input archives",
    )
    parser.add_argument("--omit", type=Path)
    parser.add_argument("--allow", type=Path)
    parser.add_argument("--dependency", action="append", default=[], type=Path)
    parser.add_argument("archives", nargs="+", type=Path)
    arguments = parser.parse_args()

    archive_symbols: set[str] = set()
    for archive in arguments.archives:
        archive_symbols.update(read_symbols(arguments.readelf, archive))
    dependency_symbols: set[str] = set()
    for dependency in arguments.dependency:
        dependency_symbols.update(
            read_symbols(arguments.readelf, dependency, dynamic=True))

    omitted = (omit_symbols(arguments.omit, archive_symbols)
               if arguments.omit is not None else set())
    selected = archive_symbols - dependency_symbols - omitted
    if arguments.allow is not None:
        allowed = explicit_symbols(arguments.allow)
        unavailable = allowed - archive_symbols
        if unavailable:
            names = ", ".join(sorted(unavailable))
            raise ValueError(f"{arguments.allow}: allowed symbols not visible "
                             f"in archives: {names}")
        selected &= allowed
    extra = explicit_symbols(arguments.extra)
    exports = selected | extra
    write_version_map(arguments.exports_out, arguments.version, exports)
    write_undefined_response(
        arguments.undefined_out,
        selected | extra if arguments.force_extra else selected,
    )


if __name__ == "__main__":
    main()
