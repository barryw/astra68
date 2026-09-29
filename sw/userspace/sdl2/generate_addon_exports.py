#!/usr/bin/env python3
"""Export only built public symbols from an upstream SDL2 add-on header."""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from generate_archive_contract import read_symbols, write_undefined_response, write_version_map


def public_symbols(header: Path, prefix: str) -> set[str]:
    return set(re.findall(r"\b" + re.escape(prefix) + r"[A-Za-z0-9_]+\b",
                          header.read_text()))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--required", action="append", default=[])
    parser.add_argument("--exports-out", required=True, type=Path)
    parser.add_argument("--undefined-out", required=True, type=Path)
    parser.add_argument("objects", nargs="+", type=Path)
    args = parser.parse_args()
    built = set().union(*(read_symbols(args.readelf, item)
                          for item in args.objects))
    exports = public_symbols(args.header, args.prefix) & built
    missing = set(args.required) - exports
    if missing:
        raise ValueError(f"SDL add-on public ABI missing: {sorted(missing)}")
    write_version_map(args.exports_out, args.version, exports)
    write_undefined_response(args.undefined_out, exports)


if __name__ == "__main__":
    main()
