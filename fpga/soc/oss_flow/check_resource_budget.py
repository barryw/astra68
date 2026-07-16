#!/usr/bin/env python3
"""Fail a nextpnr build that consumes Astra's reserved FPGA capacity."""

import argparse
import json
from pathlib import Path
import sys


def load_json(path: Path) -> dict:
    try:
        with path.open(encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", nargs="?", type=Path, help="nextpnr JSON report")
    parser.add_argument(
        "--budget-file",
        type=Path,
        default=Path(__file__).with_name("resource_budgets.json"),
    )
    parser.add_argument(
        "--profile", help="budget profile; defaults to the configured profile"
    )
    parser.add_argument(
        "--validate-profile",
        action="store_true",
        help="validate that the selected profile exists, then exit",
    )
    args = parser.parse_args()

    try:
        budgets = load_json(args.budget_file)
        profile_name = args.profile or budgets["default_profile"]
        profile = budgets["profiles"][profile_name]
        if args.validate_profile:
            print(f"Resource profile: {profile_name} - {profile['description']}")
            return 0
        if args.report is None:
            parser.error("report is required unless --validate-profile is used")
        report = load_json(args.report)
        expected = budgets["device"]["resources"]
        absolute_max = budgets["absolute_max"]
        utilization = report["utilization"]
    except (KeyError, TypeError, ValueError) as error:
        print(f"resource budget error: {error}", file=sys.stderr)
        return 2

    failures = []
    print(f"Resource budget: {profile_name} - {profile['description']}")
    profile_maxima = profile["max"]
    tracked_resources = set(absolute_max)
    missing_resources = tracked_resources - set(profile_maxima)
    extra_resources = set(profile_maxima) - tracked_resources
    for resource in sorted(missing_resources):
        failures.append(f"{resource}: missing from {profile_name} profile")
    for resource in sorted(extra_resources):
        failures.append(f"{resource}: profile has no absolute maximum")

    for resource in sorted(tracked_resources - missing_resources):
        profile_max = profile_maxima[resource]
        try:
            used = int(utilization[resource]["used"])
            available = int(utilization[resource]["available"])
            device_available = int(expected[resource])
            hard_max = int(absolute_max[resource])
        except (KeyError, TypeError, ValueError) as error:
            failures.append(f"{resource}: malformed or missing utilization ({error})")
            continue

        if available != device_available:
            failures.append(
                f"{resource}: report is for {available} units, "
                f"expected {device_available}"
            )
        if profile_max > hard_max:
            failures.append(
                f"{resource}: profile maximum {profile_max} exceeds "
                f"absolute maximum {hard_max}"
            )
        if used > profile_max:
            failures.append(
                f"{resource}: uses {used}, exceeding {profile_name} "
                f"maximum {profile_max}"
            )
        if used > hard_max:
            failures.append(
                f"{resource}: uses {used}, exceeding absolute maximum {hard_max}"
            )

        percent = 100.0 * used / available
        print(
            f"  {resource}: {used}/{available} ({percent:.2f}%), "
            f"profile headroom {profile_max - used}, "
            f"physical reserve {available - used}"
        )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("Resource budget PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
