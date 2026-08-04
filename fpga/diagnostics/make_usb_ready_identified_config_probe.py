#!/usr/bin/env python3
"""Make identified USB-ready probes from an unpacked ECP5 configuration."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


USB_TILE = "R48C36:PLC2"
SYNC_TILE = "R51C42:PLC2"
BUILD_TILE = "R59C49:PLC2"
ONE = "1111111111111111"

IDENTITY_PATCH = {
    (BUILD_TILE, "word", "SLICED.K1.INIT"): ("0000000000000000", ONE),
}
PROBE_PATCHES = {
    "predicate-high": {
        (USB_TILE, "word", "SLICEA.K0.INIT"): ("0000000001000000", ONE),
        (USB_TILE, "word", "SLICEA.K1.INIT"): ("0000000000000000", ONE),
    },
    "final-ff-high": {
        (SYNC_TILE, "enum", "SLICEC.CEMUX"): ("1", "0"),
        (SYNC_TILE, "enum", "SLICEC.REG1.REGSET"): ("RESET", "SET"),
    },
    "other-ff-high": {
        (SYNC_TILE, "enum", "SLICEB.CEMUX"): ("1", "0"),
        (SYNC_TILE, "enum", "SLICEB.REG0.REGSET"): ("RESET", "SET"),
    },
}


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def patch_config(text: str, probe: str) -> tuple[str, list[str]]:
    patches = IDENTITY_PATCH | PROBE_PATCHES[probe]
    current_tile: str | None = None
    seen: set[tuple[str, str, str]] = set()
    changed: list[str] = []
    output: list[str] = []

    for line in text.splitlines(keepends=True):
        stripped = line.rstrip("\r\n")
        if stripped.startswith(".tile "):
            current_tile = stripped.removeprefix(".tile ")
            output.append(line)
            continue
        if current_tile is None or not (
            stripped.startswith("word: ") or stripped.startswith("enum: ")
        ):
            output.append(line)
            continue

        fields = stripped.split()
        if len(fields) != 3:
            output.append(line)
            continue
        directive, item, value = fields
        directive = directive.removesuffix(":")
        key = (current_tile, directive, item)
        if key not in patches:
            output.append(line)
            continue
        if key in seen:
            raise ValueError(
                f"duplicate configuration item {current_tile} {directive} {item}"
            )
        expected, replacement = patches[key]
        if value != expected:
            raise ValueError(
                f"unexpected {current_tile} {directive} {item}: "
                f"{value}, expected {expected}"
            )
        newline = line[len(line.rstrip("\r\n")) :]
        output.append(f"{directive}: {item} {replacement}{newline}")
        seen.add(key)
        changed.append(
            f"{current_tile}:{directive}:{item}:{expected}->{replacement}"
        )

    missing = patches.keys() - seen
    if missing:
        names = ", ".join(
            f"{tile}:{directive}:{item}"
            for tile, directive, item in sorted(missing)
        )
        raise ValueError(f"missing configuration items: {names}")
    return "".join(output), changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--probe", choices=tuple(PROBE_PATCHES), default="predicate-high"
    )
    args = parser.parse_args()

    source = args.input.read_text(encoding="ascii")
    patched, changes = patch_config(source, args.probe)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(patched, encoding="ascii")

    print(f"probe=usb-ready-{args.probe}-identified")
    for change in changes:
        print(f"changed={change}")
    print(f"input_sha256={file_hash(args.input)}")
    print(f"output_sha256={file_hash(args.output)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
