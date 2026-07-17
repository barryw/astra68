#!/usr/bin/env python3
"""Fail packaging unless every required Astra clock meets its constraint."""

import argparse
import json
import math
from pathlib import Path
import sys


REQUIRED_CLOCKS = {
    "$glbnet$clk": 12.5,
    "$glbnet$sd_clk_in": 20.0,
    "$glbnet$sdram_domain_clk": 75.0,
    "$glbnet$video_pixel_clk": 27.0,
    "$glbnet$video_shift_clk": 135.0,
    "clk25_mhz$TRELLIS_IO_IN": 25.0,
}


def load_json(path: Path) -> dict:
    try:
        with path.open(encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error


def check_fmax(report: dict) -> tuple[list[str], list[tuple[str, float, float]]]:
    try:
        fmax = report["fmax"]
    except (KeyError, TypeError) as error:
        raise ValueError("nextpnr report is missing fmax") from error
    if not isinstance(fmax, dict):
        raise ValueError("nextpnr fmax entry is not an object")

    failures = []
    measurements = []
    for clock, required in sorted(REQUIRED_CLOCKS.items()):
        if clock not in fmax:
            failures.append(f"{clock}: missing from timing report")
            continue
        try:
            achieved = float(fmax[clock]["achieved"])
            constraint = float(fmax[clock]["constraint"])
        except (KeyError, TypeError, ValueError) as error:
            failures.append(f"{clock}: malformed timing result ({error})")
            continue
        if not math.isfinite(achieved) or not math.isfinite(constraint):
            failures.append(f"{clock}: non-finite timing result")
            continue
        measurements.append((clock, achieved, constraint))
        if constraint < required:
            failures.append(
                f"{clock}: reported constraint {constraint:.6f} MHz "
                f"is below required {required:.6f} MHz"
            )
        if achieved < max(constraint, required):
            failures.append(
                f"{clock}: {achieved:.6f} MHz is below required "
                f"{max(constraint, required):.6f} MHz"
            )
    return failures, measurements


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path, help="nextpnr JSON report")
    args = parser.parse_args()

    try:
        failures, measurements = check_fmax(load_json(args.report))
    except ValueError as error:
        print(f"timing gate error: {error}", file=sys.stderr)
        return 2

    print("Timing gate:")
    for clock, achieved, constraint in measurements:
        print(
            f"  {clock}: {achieved:.6f} MHz "
            f"(constraint {constraint:.6f} MHz)"
        )
    if failures:
        sys.stdout.flush()
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("Timing gate PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
