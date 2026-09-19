#!/usr/bin/env python3
"""Generate an ELF version map from macro-marked C API declarations.

Several upstream libraries already mark their public declarations with API
macros.  Those headers are the authority: deriving the version map from them
avoids a second symbol list that can silently drift during an upgrade.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

try:
    from tools.generate_archive_contract import IDENTIFIER, write_version_map
except ModuleNotFoundError:  # Direct execution from the tools directory.
    from generate_archive_contract import IDENTIFIER, write_version_map


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def declared_functions(text: str, macros: list[str]) -> set[str]:
    """Return function names from declarations carrying an API macro."""
    if not macros:
        raise ValueError("at least one API macro is required")
    alternatives = "|".join(re.escape(macro) for macro in macros)
    declaration = re.compile(
        rf"\b(?:{alternatives})\b[^;]*?\(\s*"
        rf"([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\(",
        flags=re.DOTALL,
    )
    return set(declaration.findall(strip_comments(text)))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--macro", required=True, action="append")
    parser.add_argument("--extra", action="append", default=[])
    parser.add_argument("headers", nargs="+", type=Path)
    arguments = parser.parse_args()

    invalid = [name for name in arguments.extra
               if IDENTIFIER.fullmatch(name) is None]
    if invalid:
        raise ValueError(f"invalid extra export: {invalid[0]}")
    source = "\n".join(header.read_text(encoding="utf-8")
                       for header in arguments.headers)
    exports = declared_functions(source, arguments.macro)
    exports.update(arguments.extra)
    write_version_map(arguments.output, arguments.version, exports)


if __name__ == "__main__":
    main()
