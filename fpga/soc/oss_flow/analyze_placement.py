#!/usr/bin/env python3
"""Summarize subsystem placement and long, high-fanout nets from nextpnr JSON."""

from __future__ import annotations

import argparse
import json
import math
import re
from collections import defaultdict
from pathlib import Path


GROUPS = (
    ("hdmi_ser_gpdi", ("lattice_ecp5_shift", "load_channel", "tmds_shift", "tmds_pair")),
    ("tg68k_cache", ("tg_cache_store_i",)),
    ("tg68k_pmmu", ("\\pmmu_030.", ".pmmu_030.")),
    ("tg68k_alu", ("\\alu.", ".alu.")),
    ("hdmi_pixel", ("hdmi_inst", "packet_picker", "packet_assembler")),
    ("sdram_edge", ("sdram_i", "sdram_inst", "sdram_bridge_i")),
    ("host_io", ("host_spi_i", "host_boot_i", "spi_sd_i", "uart_i", "uart_rx_fifo_i")),
    ("post_console", ("post_console_i", "char_mem", "font_mem")),
    ("astraea_draw", ("astraea_i.\\draw_i", "astraea_i.draw_i", "\\draw_i.")),
    ("vega_sprites", ("sprite_builder_i",)),
    ("vega_tiles", ("tile_builder_i",)),
    ("astraea_copper", ("astraea_i.\\copper_i", "astraea_i.copper_i", "\\copper_i.")),
    (
        "astraea_blitter_control",
        (
            "blitter_i.cfg_op_mem",
            "blitter_i.cfg_mode_mem",
            "blitter_i.cfg_dim_mem",
            "blitter_i.state_mem",
            "blitter_i.rows_remaining_mem",
            "blitter_i.units_done_mem",
            "blitter_i.km_elements_remaining_mem",
        ),
    ),
    (
        "astraea_blitter_cdc",
        (
            "blitter_i.cfg_",
            "blitter_i.start_sync_mem",
            "blitter_i.start_seen_mem",
            "blitter_i.start_toggle_cpu",
        ),
    ),
    ("astraea_blitter", ("astraea_i.\\blitter_i", "astraea_i.blitter_i", "\\blitter_i.")),
    ("vega_video", ("g_sdram_enabled.vega_i", "\\vega_i.")),
    ("cpu_mem", ("g_tg68k_enabled.tg_cpu", "\\tg_cpu", "boot_memory_map_i")),
    ("bus_masters", ("sdram_bist_i", "host_boot_i")),
    ("astraea_core", ("g_sdram_enabled.astraea_i", "\\astraea_i.")),
)

BEL_RE = re.compile(r"^X(?P<x>\d+)/Y(?P<y>\d+)/")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json", type=Path, help="placed nextpnr JSON")
    parser.add_argument("--limit", type=int, default=30, help="long-net rows to print")
    parser.add_argument("--min-fanout", type=int, default=4, help="minimum connected cells")
    return parser.parse_args()


def choose_top(modules: dict[str, object]) -> tuple[str, dict[str, object]]:
    if "top" in modules:
        return "top", modules["top"]
    if len(modules) != 1:
        raise ValueError("expected a single top module")
    return next(iter(modules.items()))


def classify(text: list[str]) -> str:
    for name, needles in GROUPS:
        if any(needle in value for value in text for needle in needles):
            return name
    return "other"


def bel_xy(cell: dict[str, object]) -> tuple[int, int] | None:
    bel = cell.get("attributes", {}).get("NEXTPNR_BEL")
    if not isinstance(bel, str):
        return None
    match = BEL_RE.match(bel)
    if match is None:
        return None
    return int(match.group("x")), int(match.group("y"))


def preferred_net_name(names: list[str]) -> str:
    visible = [name for name in names if not name.startswith("$")]
    candidates = visible or names
    return min(candidates, key=lambda name: (len(name), name)) if candidates else "<unnamed>"


def percentile(values: list[int], fraction: float) -> int:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, math.floor(fraction * len(ordered)))]


def main() -> None:
    args = parse_args()
    design = json.loads(args.json.read_text())
    top_name, top = choose_top(design["modules"])

    bit_names: dict[int, list[str]] = defaultdict(list)
    global_bits: set[int] = set()
    for name, net in top.get("netnames", {}).items():
        attributes = net.get("attributes", {})
        is_global = attributes.get("ECP5_IS_GLOBAL") not in (None, 0, "0", "")
        for bit in net.get("bits", []):
            if not isinstance(bit, int):
                continue
            bit_names[bit].append(name)
            if is_global:
                global_bits.add(bit)

    group_points: dict[str, list[tuple[int, int]]] = defaultdict(list)
    bit_cells: dict[int, dict[str, tuple[str, int, int]]] = defaultdict(dict)
    for cell_name, cell in top.get("cells", {}).items():
        point = bel_xy(cell)
        if point is None:
            continue
        connected_names = {
            net_name
            for bits in cell.get("connections", {}).values()
            for bit in bits
            if isinstance(bit, int)
            for net_name in bit_names.get(bit, ())
        }
        group = classify([cell_name, *connected_names])
        x, y = point
        group_points[group].append(point)
        for bits in cell.get("connections", {}).values():
            for bit in bits:
                if isinstance(bit, int):
                    bit_cells[bit][cell_name] = (group, x, y)

    print(f"module={top_name} placed_cells={sum(map(len, group_points.values()))}")
    print("group                 cells      x-range      y-range      centroid       p90-radius")
    for group, points in sorted(group_points.items(), key=lambda item: (-len(item[1]), item[0])):
        xs = [point[0] for point in points]
        ys = [point[1] for point in points]
        avg_x = sum(xs) / len(xs)
        avg_y = sum(ys) / len(ys)
        radii = [round(abs(x - avg_x) + abs(y - avg_y)) for x, y in points]
        print(
            f"{group:20} {len(points):6d}  {min(xs):3d}..{max(xs):3d}  "
            f"{min(ys):3d}..{max(ys):3d}  {avg_x:6.1f},{avg_y:5.1f}  {percentile(radii, 0.90):6d}"
        )

    long_nets = []
    for bit, cells_by_name in bit_cells.items():
        cells = list(cells_by_name.values())
        if len(cells) < args.min_fanout or bit in global_bits:
            continue
        xs = [cell[1] for cell in cells]
        ys = [cell[2] for cell in cells]
        span = max(xs) - min(xs) + max(ys) - min(ys)
        groups = sorted({cell[0] for cell in cells})
        score = span * math.sqrt(len(cells))
        long_nets.append(
            (score, span, len(cells), preferred_net_name(bit_names.get(bit, [])), groups)
        )

    print("\nscore     span fanout groups net")
    for score, span, fanout, name, groups in sorted(long_nets, reverse=True)[: args.limit]:
        print(f"{score:8.1f} {span:5d} {fanout:6d} {','.join(groups):32} {name}")


if __name__ == "__main__":
    main()
