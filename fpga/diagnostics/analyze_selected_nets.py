#!/usr/bin/env python3
"""Report physical connectivity for selected nets in a nextpnr JSON design."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
import re
from pathlib import Path


BEL_RE = re.compile(r"^X(?P<x>\d+)/Y(?P<y>\d+)/")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json", type=Path, help="placed or routed nextpnr JSON")
    parser.add_argument(
        "--net",
        action="append",
        required=True,
        dest="nets",
        help="scalar net name to inspect; may be repeated",
    )
    parser.add_argument(
        "--sink-limit",
        type=int,
        default=0,
        help="list individual sinks when fanout does not exceed this value",
    )
    parser.add_argument(
        "--driver-parameters",
        action="store_true",
        help="print primitive parameters for each driver cell",
    )
    return parser.parse_args()


def choose_top(modules: dict[str, object]) -> tuple[str, dict[str, object]]:
    if "top" in modules:
        return "top", modules["top"]
    if len(modules) != 1:
        raise ValueError("expected a single top module")
    return next(iter(modules.items()))


def bel_point(cell: dict[str, object]) -> tuple[int, int] | None:
    bel = cell.get("attributes", {}).get("NEXTPNR_BEL")
    if not isinstance(bel, str):
        return None
    match = BEL_RE.match(bel)
    if match is None:
        return None
    return int(match.group("x")), int(match.group("y"))


def connection_rows(
    cells: dict[str, object], bit: int
) -> list[tuple[str, str, str, str, tuple[int, int] | None]]:
    rows = []
    for cell_name, cell in cells.items():
        directions = cell.get("port_directions", {})
        for port, bits in cell.get("connections", {}).items():
            if bit not in bits:
                continue
            rows.append(
                (
                    directions.get(port, "unknown"),
                    cell_name,
                    cell.get("type", "<unknown>"),
                    port,
                    bel_point(cell),
                )
            )
    return rows


def short_attributes(attributes: dict[str, object]) -> dict[str, object]:
    result = {}
    for name, value in sorted(attributes.items()):
        if name == "ROUTING" and isinstance(value, str):
            result[name] = {
                "bytes": len(value),
                "segments": value.count(";") + (1 if value else 0),
            }
        elif isinstance(value, str) and len(value) > 160:
            result[name] = f"<{len(value)} bytes>"
        else:
            result[name] = value
    return result


def main() -> int:
    args = parse_args()
    with args.json.open(encoding="utf-8") as stream:
        design = json.load(stream)
    top_name, top = choose_top(design["modules"])
    cells = top.get("cells", {})
    netnames = top.get("netnames", {})
    print(f"module={top_name} cells={len(cells)} file={args.json}")

    aliases: dict[int, list[str]] = defaultdict(list)
    for name, net in netnames.items():
        for bit in net.get("bits", []):
            if isinstance(bit, int):
                aliases[bit].append(name)

    failed = False
    for name in args.nets:
        net = netnames.get(name)
        if net is None:
            print(f"\n{name}: MISSING")
            failed = True
            continue
        bits = net.get("bits", [])
        if len(bits) != 1 or not isinstance(bits[0], int):
            print(f"\n{name}: expected one integer bit, got {bits!r}")
            failed = True
            continue

        bit = bits[0]
        rows = connection_rows(cells, bit)
        drivers = [row for row in rows if row[0] in ("output", "inout")]
        sinks = [row for row in rows if row[0] in ("input", "inout")]
        unknown = [row for row in rows if row[0] not in ("input", "output", "inout")]
        points = [row[4] for row in rows if row[4] is not None]
        if points:
            xs = [point[0] for point in points]
            ys = [point[1] for point in points]
            bbox = f"X{min(xs)}..{max(xs)} Y{min(ys)}..{max(ys)} span={max(xs)-min(xs)+max(ys)-min(ys)}"
        else:
            bbox = "unplaced"

        print(f"\n{name}: bit={bit} aliases={aliases[bit]}")
        print(f"  attrs={json.dumps(short_attributes(net.get('attributes', {})), sort_keys=True)}")
        print(
            f"  drivers={len(drivers)} sinks={len(sinks)} unknown={len(unknown)} "
            f"bbox={bbox}"
        )
        for direction, cell_name, cell_type, port, point in drivers:
            bel = cells[cell_name].get("attributes", {}).get("NEXTPNR_BEL", "<unplaced>")
            print(f"  driver {cell_name} type={cell_type} port={port} bel={bel}")
            if args.driver_parameters:
                parameters = cells[cell_name].get("parameters", {})
                print(f"    parameters={json.dumps(parameters, sort_keys=True)}")
        by_type_port = Counter((row[2], row[3]) for row in sinks)
        print("  sinks_by_type_port:")
        for (cell_type, port), count in sorted(
            by_type_port.items(), key=lambda item: (-item[1], item[0])
        ):
            print(f"    {count:6d} {cell_type}.{port}")
        if args.sink_limit and len(sinks) <= args.sink_limit:
            print("  sinks:")
            for direction, cell_name, cell_type, port, point in sinks:
                bel = cells[cell_name].get("attributes", {}).get(
                    "NEXTPNR_BEL", "<unplaced>"
                )
                print(f"    {cell_name} type={cell_type} port={port} bel={bel}")
        if unknown:
            print("  unknown_connections:")
            for direction, cell_name, cell_type, port, point in unknown:
                print(f"    {cell_name} type={cell_type} port={port}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
