#!/usr/bin/env python3
"""Route retained full-SoC USB readiness signals to the ULX3S LEDs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile


LED_SIGNALS = (
    "g_sdram_enabled.sd_pll_locked",
    "g_sdram_enabled.sd_lock_sync[1]",
    "g_sdram_enabled.sd_ready",
    "g_sdram_enabled.usb_phy_pll_locked",
    "g_sdram_enabled.usb_ctrl_rst",
    "g_sdram_enabled.usb_mem_rst",
    "g_sdram_enabled.usb_phy_rst",
    "usb_ready_cpu",
)


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_design(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def patch_leds(design: dict) -> list[tuple[int, int, int, str]]:
    try:
        top = design["modules"]["top"]
        cells = top["cells"]
        netnames = top["netnames"]
    except (KeyError, TypeError) as error:
        raise ValueError("input is not a placed nextpnr top design") from error

    changes: list[tuple[int, int, int, str]] = []
    for index, signal in enumerate(LED_SIGNALS):
        try:
            bits = netnames[signal]["bits"]
            cell = cells[f"leds[{index}]$tr_io"]
            previous = cell["connections"]["I"]
        except (KeyError, TypeError) as error:
            raise ValueError(f"missing LED {index} or signal {signal}") from error
        if (
            not isinstance(bits, list)
            or len(bits) != 1
            or not isinstance(bits[0], int)
        ):
            raise ValueError(f"signal {signal} is not a scalar routed net")
        if cell.get("type") != "TRELLIS_IO" or not isinstance(previous, list) \
                or len(previous) != 1 or not isinstance(previous[0], int):
            raise ValueError(f"LED {index} is not a driven TRELLIS_IO cell")
        cell["connections"]["I"] = [bits[0]]
        changes.append((index, previous[0], bits[0], signal))
    if len({change[2] for change in changes}) != len(changes):
        raise ValueError("USB debug signals do not map to distinct routed nets")
    return changes


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    source = args.source.resolve()
    destination = args.destination.resolve()
    if source == destination:
        parser.error("destination must not overwrite the retained route input")

    design = load_design(source)
    changes = patch_leds(design)
    write_design(destination, design)
    del design

    for index, previous, replacement, signal in changes:
        print(
            f"LED{index}: net {previous} -> {replacement} ({signal})"
        )
    print(f"input sha256={file_hash(source)}")
    print(f"output sha256={file_hash(destination)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
