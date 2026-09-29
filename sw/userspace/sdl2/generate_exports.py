#!/usr/bin/env python3
"""Derive Astra's SDL2 ABI from upstream's public export list and built core."""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from generate_archive_contract import read_symbols, write_undefined_response, write_version_map


EXPORT = re.compile(r"\+\+'_SDL_[A-Za-z0-9_]+'\.'SDL2\.dll'\.'(SDL_[A-Za-z0-9_]+)'$")


def upstream_exports(path: Path) -> set[str]:
    exports = set()
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = EXPORT.fullmatch(line)
        if not match:
            raise ValueError(f"{path}:{number}: unrecognized SDL export")
        exports.add(match.group(1))
    if "SDL_Init" not in exports or "SDL_OpenAudioDevice" not in exports:
        raise ValueError(f"{path}: required SDL exports missing")
    return exports


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--exports-out", required=True, type=Path)
    parser.add_argument("--undefined-out", required=True, type=Path)
    args = parser.parse_args()
    exports = upstream_exports(args.upstream) & read_symbols(args.readelf, args.archive)
    if "SDL_Init" not in exports or "SDL_OpenAudioDevice" not in exports:
        raise ValueError("built SDL core omits required public APIs")
    write_version_map(args.exports_out, "ASTRA_SDL2_2_0", exports)
    write_undefined_response(args.undefined_out, exports)


if __name__ == "__main__":
    main()
