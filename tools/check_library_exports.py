#!/usr/bin/env python3
"""Check that a shared-library version map exports documented public APIs.

The version map is the ABI allow-list.  This gate rejects wildcard exports and
requires every exported C identifier to be declared in one of the supplied
public headers.  The NDK's strict Doxygen build independently rejects missing
inline documentation on those declarations; together the two gates prevent an
implementation symbol from becoming an undocumented ABI by accident.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def exported_symbols(path: Path) -> list[str]:
    text = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8"),
                  flags=re.DOTALL)
    blocks = re.findall(r"\bglobal\s*:(.*?)(?:\blocal\s*:|\})", text,
                        flags=re.DOTALL)
    if not blocks:
        raise ValueError(f"{path}: no global export block")
    symbols: list[str] = []
    for block in blocks:
        for entry in block.split(";"):
            symbol = entry.strip()
            if not symbol:
                continue
            if not IDENTIFIER.fullmatch(symbol):
                raise ValueError(
                    f"{path}: export must be an explicit C identifier: "
                    f"{symbol}")
            symbols.append(symbol)
    if not symbols:
        raise ValueError(f"{path}: empty global export block")
    duplicates = sorted({symbol for symbol in symbols
                         if symbols.count(symbol) != 1})
    if duplicates:
        raise ValueError(f"{path}: duplicate exports: {', '.join(duplicates)}")
    return symbols


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("version_map", type=Path)
    parser.add_argument("headers", nargs="+", type=Path)
    parser.add_argument("--allow", action="append", default=[])
    arguments = parser.parse_args()

    headers = "\n".join(path.read_text(encoding="utf-8")
                         for path in arguments.headers)
    allowed = set(arguments.allow)
    missing = []
    for symbol in exported_symbols(arguments.version_map):
        if symbol in allowed:
            continue
        if re.search(rf"\b{re.escape(symbol)}\b", headers) is None:
            missing.append(symbol)
    if missing:
        raise SystemExit(
            f"{arguments.version_map}: exports absent from public headers:\n  "
            + "\n  ".join(missing))


if __name__ == "__main__":
    main()
