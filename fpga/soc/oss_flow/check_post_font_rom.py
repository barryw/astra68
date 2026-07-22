#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


FONT_CELL_PREFIX = "g_hdmi.post_console_i.font_rom."


def _parameter_int(value: object) -> int:
    if isinstance(value, int):
        return value
    if not isinstance(value, str):
        raise ValueError(f"unsupported parameter value {value!r}")
    if value.startswith("0b"):
        return int(value[2:], 2)
    if value and set(value) <= {"0", "1"}:
        return int(value, 2)
    return int(value, 0)


def check_post_font_rom(design: dict, top: str = "astra_soc") -> str:
    try:
        cells = design["modules"][top]["cells"]
    except KeyError as error:
        raise ValueError(f"synthesis JSON is missing top module {top!r}") from error

    font_cells = [
        (name, cell)
        for name, cell in cells.items()
        if name.startswith(FONT_CELL_PREFIX)
    ]
    if len(font_cells) != 1:
        names = [name for name, _ in font_cells]
        raise ValueError(
            "POST font must map to exactly one physical block RAM; "
            f"found {len(font_cells)}: {names}"
        )

    name, cell = font_cells[0]
    if cell.get("type") != "DP16KD":
        raise ValueError(f"POST font cell {name!r} is not a DP16KD")

    width = _parameter_int(cell.get("parameters", {}).get("DATA_WIDTH_A"))
    if width != 9:
        raise ValueError(f"POST font cell {name!r} has DATA_WIDTH_A={width}, not 9")

    connections = cell.get("connections", {})
    for pin in ("ADA0", "ADA1", "ADA2"):
        if connections.get(pin) != ["0"]:
            raise ValueError(f"POST font cell {name!r} has unexpected {pin} wiring")

    address_bits = []
    for bit in range(3, 14):
        pin = f"ADA{bit}"
        connection = connections.get(pin)
        if not isinstance(connection, list) or len(connection) != 1:
            raise ValueError(f"POST font cell {name!r} is missing {pin}")
        signal = connection[0]
        if not isinstance(signal, int):
            raise ValueError(
                f"POST font cell {name!r} has constant logical address pin {pin}"
            )
        address_bits.append(signal)

    if len(set(address_bits)) != 11:
        raise ValueError(f"POST font cell {name!r} does not use 11 unique address bits")

    return name


def main() -> int:
    parser = argparse.ArgumentParser(
        description="verify exact-depth ECP5 POST font block-RAM mapping"
    )
    parser.add_argument("synthesis_json", type=Path)
    parser.add_argument("--top", default="astra_soc")
    args = parser.parse_args()

    with args.synthesis_json.open(encoding="utf-8") as source:
        design = json.load(source)

    cell_name = check_post_font_rom(design, args.top)
    print(f"POST font ROM: PASS ({cell_name}, one DP16KD, 11 address bits)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
