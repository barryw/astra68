#!/usr/bin/env python3
"""Reject ECP5 routes that violate mode-specific LUT input restrictions."""

import argparse
import json
from pathlib import Path
import re


BEL_RE = re.compile(r"^X(?P<x>\d+)/Y(?P<y>\d+)/SLICE(?P<slice>[A-D])\.K(?P<half>[01])$")
WIRE_RE = re.compile(r"(?:^|_)(?P<input>[A-D])(?P<lut>[0-7])(?:_SLICE)?$")


def routed_input_mappings(route):
    mappings = {}
    fields = route.split(";")
    for index in range(1, len(fields), 3):
        pip = fields[index]
        if "->" not in pip:
            continue
        location, arc = pip.rsplit("/", 1)
        source, destination = arc.split("->", 1)
        source_match = WIRE_RE.search(source)
        destination_match = WIRE_RE.search(destination)
        if source_match is None or destination_match is None:
            continue
        if not destination.endswith("_SLICE"):
            continue
        mappings[(location, destination_match["input"], int(destination_match["lut"]))] = (
            source_match["input"],
            int(source_match["lut"]),
        )
    return mappings


def bit_routes(top):
    routes = {}
    for net in top.get("netnames", {}).values():
        route = net.get("attributes", {}).get("ROUTING")
        if not route:
            continue
        mapping = routed_input_mappings(route)
        for bit in net.get("bits", []):
            if isinstance(bit, int):
                routes.setdefault(bit, {}).update(mapping)
    return routes


def check_lut_permutations(design):
    modules = design.get("modules", {})
    if len(modules) != 1:
        raise ValueError("routed design must contain exactly one flattened module")
    top = next(iter(modules.values()))
    routes = bit_routes(top)
    checked_cells = 0
    checked_inputs = 0
    failures = []

    for cell_name, cell in top.get("cells", {}).items():
        if cell.get("type") != "TRELLIS_COMB":
            continue
        mode = cell.get("parameters", {}).get("MODE", "LOGIC")
        if mode not in {"CCU2", "DPRAM", "RAMW_BLOCK"}:
            continue

        bel_text = cell.get("attributes", {}).get("NEXTPNR_BEL", "")
        match = BEL_RE.fullmatch(bel_text)
        if match is None:
            failures.append(f"{cell_name}: malformed or missing BEL {bel_text!r}")
            continue
        checked_cells += 1
        location = f"X{match['x']}/Y{match['y']}"
        lut = (ord(match["slice"]) - ord("A")) * 2 + int(match["half"])

        for logical_index, port in enumerate("ABCD"):
            connection = cell.get("connections", {}).get(port)
            if not connection:
                continue
            if len(connection) != 1 or not isinstance(connection[0], int):
                failures.append(f"{cell_name}.{port}: unexpected connection {connection!r}")
                continue
            checked_inputs += 1
            physical = routes.get(connection[0], {}).get((location, port, lut))
            if physical is None:
                failures.append(
                    f"{cell_name}.{port}: no routed LUT input at {location}/{port}{lut}_SLICE"
                )
                continue
            physical_port, physical_lut = physical
            if physical_lut != lut:
                failures.append(
                    f"{cell_name}.{port}: physical LUT index {physical_lut} != {lut}"
                )
                continue
            physical_index = ord(physical_port) - ord("A")
            if mode == "CCU2" and physical_index // 2 != logical_index // 2:
                failures.append(
                    f"{cell_name}.{port}: invalid CCU2 permutation "
                    f"{physical_port}{lut}->{port}{lut}"
                )
            elif mode in {"DPRAM", "RAMW_BLOCK"} and physical_port != port:
                failures.append(
                    f"{cell_name}.{port}: invalid {mode} permutation "
                    f"{physical_port}{lut}->{port}{lut}"
                )

    if checked_cells == 0:
        failures.append("routed design contains no protected ECP5 LUT modes")
    return checked_cells, checked_inputs, failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("routed_json", type=Path)
    args = parser.parse_args()
    with args.routed_json.open(encoding="utf-8") as stream:
        design = json.load(stream)

    cells, inputs, failures = check_lut_permutations(design)
    if failures:
        preview = "\n".join(f"  {failure}" for failure in failures[:20])
        remainder = len(failures) - min(len(failures), 20)
        if remainder:
            preview += f"\n  ... and {remainder} more"
        raise SystemExit(
            f"ECP5 LUT-permutation gate failed ({len(failures)} errors):\n{preview}"
        )
    print(f"ECP5 LUT-permutation gate passed: {cells} cells, {inputs} routed inputs")


if __name__ == "__main__":
    main()
