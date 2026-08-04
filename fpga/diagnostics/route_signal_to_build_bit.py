#!/usr/bin/env python3
"""Expose one routed scalar signal through retained hardware build-ID bit 0."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile


BUILD_CELL = "g_build_id_lut[0].build_id_lut_i"


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_design(path: Path, design: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as stream:
            temporary_name = stream.name
            json.dump(design, stream, separators=(",", ":"))
            stream.write("\n")
        os.replace(temporary_name, path)
    except OSError:
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except OSError:
                pass
        raise


def expose_signal(design: dict, signal: str) -> tuple[int, str]:
    try:
        top = design["modules"]["top"]
        bits = top["netnames"][signal]["bits"]
        cell = top["cells"][BUILD_CELL]
    except (KeyError, TypeError) as error:
        raise ValueError(f"missing routed signal {signal!r} or {BUILD_CELL}") from error
    if len(bits) != 1 or not isinstance(bits[0], int):
        raise ValueError(f"routed signal {signal!r} is not scalar")
    if (
        cell.get("type") != "TRELLIS_COMB"
        or cell.get("parameters", {}).get("INITVAL")
        != "0000000000000000"
        or cell.get("connections") != {"F": cell.get("connections", {}).get("F")}
    ):
        raise ValueError("build-ID bit 0 is not the expected constant-zero LUT")
    bel = cell.get("attributes", {}).get("NEXTPNR_BEL", "")
    if not bel:
        raise ValueError("build-ID bit 0 has no retained BEL")

    cell["parameters"]["INITVAL"] = "1010101010101010"
    cell["connections"]["A"] = [bits[0]]
    return bits[0], bel


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("signal")
    args = parser.parse_args()

    source = args.source.resolve()
    destination = args.destination.resolve()
    if source == destination:
        parser.error("destination must not overwrite the retained route")
    with source.open(encoding="utf-8") as stream:
        design = json.load(stream)
    bit, bel = expose_signal(design, args.signal)
    write_design(destination, design)

    print(f"signal={args.signal}")
    print(f"signal_bit={bit}")
    print(f"build_cell={BUILD_CELL}")
    print(f"build_bel={bel}")
    print(f"input_sha256={file_hash(source)}")
    print(f"output_sha256={file_hash(destination)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
