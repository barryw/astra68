#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


def check_por(design: dict, top: str = "astra_soc") -> int:
    try:
        modules = design["modules"]
        modules[top]
    except KeyError as error:
        raise ValueError(f"synthesis JSON is missing top module {top!r}") from error

    gsr_cells = [
        (module_name, cell_name)
        for module_name, module in modules.items()
        for cell_name, cell in module.get("cells", {}).items()
        if cell.get("type") == "GSR"
    ]
    if len(gsr_cells) != 1 or gsr_cells[0][0] != top:
        raise ValueError(
            "synthesis JSON must contain exactly one physical GSR primitive "
            f"in top module {top!r}; found {gsr_cells}"
        )

    flip_flops = [
        (module_name, cell_name, cell)
        for module_name, module in modules.items()
        for cell_name, cell in module.get("cells", {}).items()
        if cell.get("type") == "TRELLIS_FF"
    ]
    if not flip_flops:
        raise ValueError("synthesis JSON contains no TRELLIS_FF cells")

    unsafe = [
        (module_name, cell_name)
        for module_name, cell_name, cell in flip_flops
        if cell.get("parameters", {}).get("GSR") != "ENABLED"
    ]
    if unsafe:
        examples = ", ".join(
            f"{module_name}.{cell_name}"
            for module_name, cell_name in sorted(unsafe)[:5]
        )
        raise ValueError(
            f"{len(unsafe)} of {len(flip_flops)} flip-flops lack deterministic "
            f"configuration-time GSR (examples: {examples})"
        )

    return len(flip_flops)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="verify deterministic ECP5 configuration-time reset"
    )
    parser.add_argument("synthesis_json", type=Path)
    parser.add_argument("--top", default="astra_soc")
    args = parser.parse_args()

    with args.synthesis_json.open(encoding="utf-8") as source:
        design = json.load(source)

    count = check_por(design, args.top)
    print(f"POR GSR: PASS ({count} TRELLIS_FF cells enabled)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
