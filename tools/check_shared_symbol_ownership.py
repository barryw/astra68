#!/usr/bin/env python3
"""Reject private copies of symbols already owned by shared dependencies.

A version script can hide a duplicate definition without removing its machine
code.  This check therefore compares every named definition in the consumer's
full symbol table with the public dynamic definitions of its dependencies.
The build fails even when the consumer accidentally localized the duplicate.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def definitions(output: str, *, public_only: bool) -> set[str]:
    """Return named definitions from GNU readelf symbol-table output."""
    symbols: set[str] = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].rstrip(":").isdigit():
            continue
        _, _, _, kind, binding, visibility, section, name, *_ = fields
        if section in {"UND", "UNDEF", "ABS"} or kind in {"FILE", "SECTION"}:
            continue
        if public_only and (binding not in {"GLOBAL", "WEAK"} or
                            visibility not in {"DEFAULT", "PROTECTED"}):
            continue
        name = name.split("@", 1)[0]
        if IDENTIFIER.fullmatch(name):
            symbols.add(name)
    return symbols


def read_definitions(readelf: str, path: Path, *, dynamic: bool) -> set[str]:
    option = "--dyn-syms" if dynamic else "--syms"
    result = subprocess.run(
        [readelf, "--wide", option, str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return definitions(result.stdout, public_only=dynamic)


def duplicate_owners(consumer: set[str], dependencies: set[str],
                     allowed: set[str]) -> list[str]:
    """Return definitions that improperly have two implementation owners."""
    return sorted((consumer & dependencies) - allowed)


def duplicate_library_owners(
    libraries: dict[Path, tuple[set[str], set[str]]], allowed: set[str]
) -> dict[Path, list[str]]:
    """Return private/public definitions also exported by another library."""
    duplicates: dict[Path, list[str]] = {}
    for path, (all_definitions, _) in libraries.items():
        other_public: set[str] = set()
        for other_path, (_, public_definitions) in libraries.items():
            if other_path != path:
                other_public.update(public_definitions)
        owned_twice = duplicate_owners(all_definitions, other_public, allowed)
        if owned_twice:
            duplicates[path] = owned_twice
    return duplicates


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--allow", action="append", default=[])
    parser.add_argument("--all-libraries", action="store_true")
    parser.add_argument("libraries", nargs="+", type=Path)
    arguments = parser.parse_args()

    if arguments.all_libraries:
        if len(arguments.libraries) < 2:
            parser.error("--all-libraries requires at least two libraries")
        libraries = {
            path: (
                read_definitions(arguments.readelf, path, dynamic=False),
                read_definitions(arguments.readelf, path, dynamic=True),
            )
            for path in arguments.libraries
        }
        duplicates = duplicate_library_owners(
            libraries, set(arguments.allow)
        )
        if duplicates:
            lines = []
            for path, symbols in duplicates.items():
                lines.append(f"{path}: duplicates symbols owned by another "
                             "shared library:")
                lines.extend(f"  {symbol}" for symbol in symbols)
            raise SystemExit("\n".join(lines))
        return

    if len(arguments.libraries) < 2:
        parser.error("requires one consumer and at least one dependency")
    consumer_path, *dependency_paths = arguments.libraries

    consumer = read_definitions(arguments.readelf, consumer_path,
                                dynamic=False)
    dependencies: set[str] = set()
    for dependency in dependency_paths:
        dependencies.update(read_definitions(arguments.readelf, dependency,
                                             dynamic=True))
    duplicates = duplicate_owners(consumer, dependencies,
                                  set(arguments.allow))
    if duplicates:
        raise SystemExit(
            f"{consumer_path}: duplicates symbols owned by shared "
            "dependencies:\n  " + "\n  ".join(duplicates)
        )


if __name__ == "__main__":
    main()
