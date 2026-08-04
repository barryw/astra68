#!/usr/bin/env python3
"""Build route-preserving USB-readiness probes from a production route."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


PROBE_INITS = {
    "high": ("1111111111111111", "1111111111111111"),
    "lock": ("1111000011110000", "1111000011110000"),
    "ctrl-released": ("1111111111111111", "0000000000000000"),
    "mem-released": ("0011001100110011", "0011001100110011"),
    "phy-released": ("0101010101010101", "0101010101010101"),
}


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


def scalar_net(top: dict, name: str) -> int:
    try:
        bits = top["netnames"][name]["bits"]
    except (KeyError, TypeError) as error:
        raise ValueError(f"missing routed net {name}") from error
    if len(bits) != 1 or not isinstance(bits[0], int):
        raise ValueError(f"routed net {name} is not scalar")
    return bits[0]


def single_cell(cells: dict, predicate, description: str) -> tuple[str, dict]:
    matches = [(name, cell) for name, cell in cells.items() if predicate(cell)]
    if len(matches) != 1:
        raise ValueError(f"expected one {description}, found {len(matches)}")
    return matches[0]


def connection_is(cell: dict, port: str, bit: int) -> bool:
    return cell.get("connections", {}).get(port) == [bit]


def patch_predicate(design: dict, probe: str) -> tuple[str, ...]:
    try:
        top = design["modules"]["top"]
        cells = top["cells"]
    except (KeyError, TypeError) as error:
        raise ValueError("input is not a routed nextpnr top design") from error

    if probe == "build-bit0":
        try:
            build_cell = cells["g_build_id_lut[0].build_id_lut_i"]
        except KeyError as error:
            raise ValueError("routed design has no retained build-ID bit 0") from error
        if (
            build_cell.get("type") != "TRELLIS_COMB"
            or build_cell.get("parameters", {}).get("INITVAL")
            != "0000000000000000"
        ):
            raise ValueError("build-ID bit 0 is not the expected constant-zero LUT")
        build_cell["parameters"]["INITVAL"] = "1111111111111111"
        return ("g_build_id_lut[0].build_id_lut_i",)

    first_stage = scalar_net(top, "g_sdram_enabled.usb_ready_sync_cpu[0]")
    final_stage = scalar_net(top, "usb_ready_cpu")
    _, ready_ff = single_cell(
        cells,
        lambda cell: cell.get("type") == "TRELLIS_FF"
        and connection_is(cell, "Q", first_stage),
        "first USB-ready synchronizer flip-flop",
    )
    if probe == "consumer-high":
        consumers = [
            (name, cell)
            for name, cell in cells.items()
            if cell.get("type") == "TRELLIS_COMB"
            and connection_is(cell, "C", final_stage)
        ]
        expected = {
            "0000111100000000": "0000000000000000",
            "1111000000000000": "1111111100000000",
        }
        if len(consumers) != 2 or {
            cell.get("parameters", {}).get("INITVAL")
            for _, cell in consumers
        } != set(expected):
            raise ValueError("USB-ready consumers are not the expected two LUTs")
        for _, cell in consumers:
            parameters = cell["parameters"]
            parameters["INITVAL"] = expected[parameters["INITVAL"]]
        return tuple(name for name, _ in consumers)
    if probe == "ff-high":
        final_name, final_ff = single_cell(
            cells,
            lambda cell: cell.get("type") == "TRELLIS_FF"
            and connection_is(cell, "Q", final_stage),
            "final USB-ready synchronizer flip-flop",
        )
        parameters = final_ff.get("parameters", {})
        if (
            parameters.get("REGSET") != "RESET"
            or parameters.get("CEMUX") != "1 "
            or parameters.get("LSRMUX") != "LSR"
        ):
            raise ValueError("final USB-ready synchronizer is not enabled/reset-low")
        parameters["REGSET"] = "SET"
        parameters["CEMUX"] = "0 "
        return (final_name,)
    try:
        predicate_bit = ready_ff["connections"]["M"][0]
    except (KeyError, IndexError, TypeError) as error:
        raise ValueError("USB-ready synchronizer has no scalar M input") from error

    lut0_name, lut0 = single_cell(
        cells,
        lambda cell: cell.get("type") == "TRELLIS_COMB"
        and connection_is(cell, "OFX", predicate_bit),
        "USB-ready predicate PFUMX",
    )
    expected_inputs = {
        "A": "g_sdram_enabled.usb_phy_rst",
        "B": "g_sdram_enabled.usb_mem_rst",
        "C": "g_sdram_enabled.usb_phy_pll_locked",
        "D": "g_sdram_enabled.sd_ready",
        "M": "g_sdram_enabled.usb_ctrl_rst",
    }
    for port, net_name in expected_inputs.items():
        bit = scalar_net(top, net_name)
        if not connection_is(lut0, port, bit):
            raise ValueError(f"USB-ready predicate {port} is not {net_name}")

    try:
        lut1_bit = lut0["connections"]["F1"][0]
    except (KeyError, IndexError, TypeError) as error:
        raise ValueError("USB-ready predicate has no scalar F1 input") from error
    lut1_name, lut1 = single_cell(
        cells,
        lambda cell: cell.get("type") == "TRELLIS_COMB"
        and connection_is(cell, "F", lut1_bit),
        "USB-ready predicate companion LUT",
    )

    if lut0.get("parameters", {}).get("INITVAL") != "0001000000000000":
        raise ValueError("production USB-ready LUT0 is not the expected AND term")
    if lut1.get("parameters", {}).get("INITVAL") != "0000000000000000":
        raise ValueError("production USB-ready LUT1 is not the expected reset term")
    bel0 = lut0.get("attributes", {}).get("NEXTPNR_BEL", "")
    bel1 = lut1.get("attributes", {}).get("NEXTPNR_BEL", "")
    if not bel0 or bel0.rsplit(".K", 1)[0] != bel1.rsplit(".K", 1)[0]:
        raise ValueError("USB-ready predicate LUTs are not in one physical slice")

    lut0["parameters"]["INITVAL"], lut1["parameters"]["INITVAL"] = (
        PROBE_INITS[probe]
    )
    return lut0_name, lut1_name


def tile_blocks(text: str) -> tuple[list[str], dict[str, tuple[int, int]]]:
    lines = text.splitlines(keepends=True)
    blocks: dict[str, tuple[int, int]] = {}
    index = 0
    while index < len(lines):
        if not lines[index].startswith(".tile "):
            index += 1
            continue
        key = lines[index].removeprefix(".tile ").strip()
        end = index + 1
        while end < len(lines) and not lines[end].startswith("."):
            end += 1
        if key in blocks:
            raise ValueError(f"duplicate tile section {key}")
        blocks[key] = (index, end)
        index = end
    if not blocks:
        raise ValueError("configuration contains no tile sections")
    return lines, blocks


def block(lines: list[str], span: tuple[int, int]) -> tuple[str, ...]:
    return tuple(lines[span[0] : span[1]])


def patch_tiles(
    original_text: str,
    baseline_text: str,
    replacement_text: str,
    expected_changes: int,
) -> tuple[str, list[str]]:
    original_lines, original = tile_blocks(original_text)
    baseline_lines, baseline = tile_blocks(baseline_text)
    replacement_lines, replacement = tile_blocks(replacement_text)
    if baseline.keys() != replacement.keys():
        raise ValueError("baseline and probe tile sets differ")
    missing_original = baseline.keys() - original.keys()
    if missing_original:
        raise ValueError(
            "original configuration is missing re-emitted tiles: "
            + ", ".join(sorted(missing_original))
        )

    changed = [
        key
        for key in baseline
        if block(baseline_lines, baseline[key])
        != block(replacement_lines, replacement[key])
    ]
    if len(changed) != expected_changes:
        raise ValueError(
            f"expected {expected_changes} changed tiles, found {len(changed)}"
        )
    for key in changed:
        if block(original_lines, original[key]) != block(
            baseline_lines, baseline[key]
        ):
            raise ValueError(f"original tile {key} differs from routed JSON baseline")

    restored = replacement_lines.copy()
    for key in sorted(changed, key=lambda item: replacement[item][0], reverse=True):
        start, end = replacement[key]
        restored[start:end] = block(baseline_lines, baseline[key])
    if "".join(restored) != baseline_text:
        raise ValueError("probe re-emission changed data outside the selected tiles")

    patched = original_lines.copy()
    for key in sorted(changed, key=lambda item: original[item][0], reverse=True):
        start, end = original[key]
        patched[start:end] = block(replacement_lines, replacement[key])
    return "".join(patched), sorted(changed)


def emit_config(
    nextpnr: str,
    routed_json: Path,
    lpf: Path,
    sdc: Path,
    output: Path,
    frequency: str,
) -> None:
    result = subprocess.run(
        [
            nextpnr,
            "--85k",
            "--package",
            "CABGA381",
            "--freq",
            frequency,
            "--no-pack",
            "--no-place",
            "--no-route",
            "--json",
            str(routed_json),
            "--lpf",
            str(lpf),
            "--sdc",
            str(sdc),
            "--textcfg",
            str(output),
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode != 0:
        tail = "\n".join(result.stdout.splitlines()[-40:])
        raise RuntimeError(f"nextpnr config emission failed:\n{tail}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--routed-json", type=Path, required=True)
    parser.add_argument("--lpf", type=Path, required=True)
    parser.add_argument("--sdc", type=Path, required=True)
    parser.add_argument(
        "--probe",
        choices=tuple(PROBE_INITS)
        + ("ff-high", "consumer-high", "build-bit0"),
        required=True,
    )
    parser.add_argument("--output-config", type=Path, required=True)
    parser.add_argument("--output-bit", type=Path, required=True)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--frequency", default="12.5")
    parser.add_argument("--nextpnr")
    parser.add_argument("--ecppack")
    args = parser.parse_args()

    nextpnr = resolve_tool(args.nextpnr, "nextpnr-ecp5")
    ecppack = resolve_tool(args.ecppack, "ecppack")
    source = json.loads(args.routed_json.read_text(encoding="utf-8"))
    replacement = json.loads(args.routed_json.read_text(encoding="utf-8"))
    changed_cells = patch_predicate(replacement, args.probe)

    args.output_config.parent.mkdir(parents=True, exist_ok=True)
    args.output_bit.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="astra-usb-ready-probe-") as directory:
        temporary = Path(directory)
        baseline_json = temporary / "baseline.json"
        replacement_json = temporary / "replacement.json"
        baseline_config = temporary / "baseline.config"
        replacement_config = temporary / "replacement.config"
        baseline_json.write_text(
            json.dumps(source, separators=(",", ":")) + "\n", encoding="utf-8"
        )
        replacement_json.write_text(
            json.dumps(replacement, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        emit_config(
            nextpnr, baseline_json, args.lpf.resolve(), args.sdc.resolve(),
            baseline_config, args.frequency
        )
        emit_config(
            nextpnr, replacement_json, args.lpf.resolve(), args.sdc.resolve(),
            replacement_config, args.frequency
        )
        patched, changed_tiles = patch_tiles(
            args.config.read_text(encoding="ascii"),
            baseline_config.read_text(encoding="ascii"),
            replacement_config.read_text(encoding="ascii"),
            2 if args.probe == "consumer-high" else 1,
        )
        args.output_config.write_text(patched, encoding="ascii")
        subprocess.run(
            [ecppack, str(args.output_config), str(args.output_bit)], check=True
        )
        if args.output_json:
            args.output_json.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(replacement_json, args.output_json)

    print(f"probe={args.probe}")
    print(f"changed_cells={','.join(changed_cells)}")
    print(f"changed_tiles={','.join(changed_tiles)}")
    print(f"input_config_sha256={file_hash(args.config)}")
    print(f"output_config_sha256={file_hash(args.output_config)}")
    print(f"output_bit_sha256={file_hash(args.output_bit)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
