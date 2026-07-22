#!/usr/bin/env python3
"""Transplant a route-probe ROM into a routed image without changing routing."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from typing import Any


INITVAL_PATTERN = re.compile(r"INITVAL_[0-9A-Fa-f]{2}")
BRAM_HEADER_PATTERN = re.compile(r"\.bram_init ([0-9]+)\n?")
BRAM_ROW_PATTERN = re.compile(r"(?:[0-9A-Fa-f]{3} ){7}[0-9A-Fa-f]{3}\n?")
BRAM_ROWS = 256


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_tool(explicit: str | None, name: str) -> str:
    tool = explicit or shutil.which(name)
    if tool is None:
        raise FileNotFoundError(f"{name} is not on PATH")
    return tool


def split_init_parameters(parameters: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    ordinary = {
        name: value
        for name, value in parameters.items()
        if INITVAL_PATTERN.fullmatch(name) is None
    }
    initializers = {
        name: value
        for name, value in parameters.items()
        if INITVAL_PATTERN.fullmatch(name) is not None
    }
    return ordinary, initializers


def find_init_changes(
    source: dict[str, Any], replacement: dict[str, Any], cell_prefix: str
) -> dict[str, tuple[dict[str, Any], dict[str, Any]]]:
    def matching_cells(design: dict[str, Any]) -> dict[str, dict[str, Any]]:
        cells: dict[str, dict[str, Any]] = {}
        for module in design.get("modules", {}).values():
            for cell_name, cell in module.get("cells", {}).items():
                if not cell_name.startswith(cell_prefix):
                    continue
                if cell_name in cells:
                    raise ValueError(f"duplicate synthesis cell {cell_name}")
                cells[cell_name] = cell
        return cells

    source_cells = matching_cells(source)
    replacement_cells = matching_cells(replacement)
    if source_cells.keys() != replacement_cells.keys() or not source_cells:
        raise ValueError("synthesis ROM cell sets differ")

    changes: dict[str, tuple[dict[str, Any], dict[str, Any]]] = {}
    for cell_name, source_cell in source_cells.items():
        replacement_cell = replacement_cells[cell_name]
        if (
            source_cell.get("type") != "DP16KD"
            or replacement_cell.get("type") != "DP16KD"
        ):
            raise ValueError(f"synthesis ROM cell is not DP16KD at {cell_name}")
        source_ordinary, source_init = split_init_parameters(
            source_cell.get("parameters", {})
        )
        replacement_ordinary, replacement_init = split_init_parameters(
            replacement_cell.get("parameters", {})
        )
        if source_ordinary != replacement_ordinary:
            raise ValueError(f"non-INIT parameters differ at {cell_name}")
        if source_init != replacement_init:
            changes[cell_name] = (source_init, replacement_init)

    if not changes:
        raise ValueError("replacement synthesis has no ROM initializer changes")
    return changes


def transplant_initializers(
    routed: dict[str, Any],
    changes: dict[str, tuple[dict[str, Any], dict[str, Any]]],
) -> None:
    routed_cells = {
        cell_name: cell
        for module in routed.get("modules", {}).values()
        for cell_name, cell in module.get("cells", {}).items()
    }
    for cell_name, (source_init, replacement_init) in changes.items():
        if cell_name not in routed_cells:
            raise ValueError(f"routed design is missing {cell_name}")
        parameters = routed_cells[cell_name].get("parameters", {})
        _, routed_init = split_init_parameters(parameters)
        if routed_init != source_init:
            raise ValueError(f"routed initializer does not match source at {cell_name}")
        parameters.update(replacement_init)


def bram_blocks(lines: list[str]) -> dict[int, tuple[int, int, tuple[str, ...]]]:
    blocks: dict[int, tuple[int, int, tuple[str, ...]]] = {}
    for start, line in enumerate(lines):
        match = BRAM_HEADER_PATTERN.fullmatch(line)
        if match is None:
            continue
        block_id = int(match.group(1))
        end = start + 1 + BRAM_ROWS
        if end > len(lines) or any(
            BRAM_ROW_PATTERN.fullmatch(row) is None for row in lines[start + 1 : end]
        ):
            raise ValueError(f"malformed .bram_init {block_id} section")
        if block_id in blocks:
            raise ValueError(f"duplicate .bram_init {block_id} section")
        blocks[block_id] = (start, end, tuple(lines[start:end]))
    if not blocks:
        raise ValueError("configuration contains no BRAM initialization sections")
    return blocks


def patch_bram_blocks(
    original_text: str, replacement_text: str, expected_changes: int
) -> tuple[str, list[int]]:
    original_lines = original_text.splitlines(keepends=True)
    replacement_lines = replacement_text.splitlines(keepends=True)
    original_blocks = bram_blocks(original_lines)
    replacement_blocks = bram_blocks(replacement_lines)
    if original_blocks.keys() != replacement_blocks.keys():
        raise ValueError("configuration BRAM block sets differ")

    changed_ids = [
        block_id
        for block_id in original_blocks
        if original_blocks[block_id][2] != replacement_blocks[block_id][2]
    ]
    if len(changed_ids) != expected_changes:
        raise ValueError(
            f"expected {expected_changes} changed BRAM blocks, found {len(changed_ids)}"
        )

    patched = original_lines.copy()
    for block_id in sorted(changed_ids, key=lambda item: original_blocks[item][0], reverse=True):
        start, end, _ = original_blocks[block_id]
        patched[start:end] = replacement_blocks[block_id][2]
    return "".join(patched), sorted(changed_ids)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True, help="original routed config")
    parser.add_argument("--routed-json", type=Path, required=True)
    parser.add_argument("--from-json", type=Path, required=True, help="production synthesis JSON")
    parser.add_argument("--to-json", type=Path, required=True, help="route-probe synthesis JSON")
    parser.add_argument("--lpf", type=Path, required=True)
    parser.add_argument("--sdc", type=Path, required=True)
    parser.add_argument("--output-config", type=Path, required=True)
    parser.add_argument("--output-bit", type=Path, required=True)
    parser.add_argument("--output-json", type=Path, help="retain the INIT-modified routed JSON")
    parser.add_argument("--cell-prefix", default="rom.")
    parser.add_argument("--frequency", default="12.5")
    parser.add_argument("--nextpnr", help="nextpnr-ecp5 executable")
    parser.add_argument("--ecppack", help="ecppack executable")
    args = parser.parse_args()

    config = args.config.resolve()
    output_config = args.output_config.resolve()
    output_bit = args.output_bit.resolve()
    if config == output_config:
        parser.error("--output-config must not overwrite --config")

    nextpnr = resolve_tool(args.nextpnr, "nextpnr-ecp5")
    ecppack = resolve_tool(args.ecppack, "ecppack")
    source = json.loads(args.from_json.read_text(encoding="utf-8"))
    replacement = json.loads(args.to_json.read_text(encoding="utf-8"))
    changes = find_init_changes(source, replacement, args.cell_prefix)
    del source, replacement

    routed = json.loads(args.routed_json.read_text(encoding="utf-8"))
    transplant_initializers(routed, changes)
    output_config.parent.mkdir(parents=True, exist_ok=True)
    output_bit.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="astra-route-probe-") as directory:
        temporary = Path(directory)
        replacement_routed = temporary / "routed-route-probe.json"
        replacement_config = temporary / "route-probe-reemitted.config"
        replacement_bit = temporary / "route-probe.bit"
        replacement_routed.write_text(
            json.dumps(routed, separators=(",", ":")), encoding="utf-8"
        )
        subprocess.run(
            [
                nextpnr,
                "--85k",
                "--package",
                "CABGA381",
                "--freq",
                args.frequency,
                "--no-pack",
                "--no-place",
                "--no-route",
                "--json",
                str(replacement_routed),
                "--lpf",
                str(args.lpf.resolve()),
                "--sdc",
                str(args.sdc.resolve()),
                "--textcfg",
                str(replacement_config),
            ],
            check=True,
        )
        patched_text, changed_blocks = patch_bram_blocks(
            config.read_text(encoding="ascii"),
            replacement_config.read_text(encoding="ascii"),
            len(changes),
        )
        output_config.write_text(patched_text, encoding="ascii")
        subprocess.run([ecppack, str(output_config), str(replacement_bit)], check=True)
        shutil.copyfile(replacement_bit, output_bit)
        if args.output_json:
            args.output_json.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(replacement_routed, args.output_json)

    print(f"changed cells={','.join(sorted(changes))}")
    print(f"changed BRAM blocks={','.join(str(item) for item in changed_blocks)}")
    print(f"input config sha256={file_hash(config)}")
    print(f"output config sha256={file_hash(output_config)}")
    print(f"output bit sha256={file_hash(output_bit)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
