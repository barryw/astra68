#!/usr/bin/env python3
"""Prepare a placed nextpnr JSON for an explicit route invocation."""

import argparse
import json
import os
from pathlib import Path
import sys
import tempfile


FALSE_SETTING = "0" * 32
SERIALIZED_ROUTER_CONTROLS = (
    "router",
    "router/tmg_ripup",
    "router2/alt-weights",
)


def load_json(path: Path) -> dict:
    try:
        with path.open(encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error


def prepare_route_input(
    source: Path,
    destination: Path,
    *,
    clear_timing_waiver: bool = True,
) -> tuple[str | None, dict[str, object]]:
    design = load_json(source)
    try:
        settings = design["modules"]["top"]["settings"]
    except (KeyError, TypeError) as error:
        raise ValueError("nextpnr design is missing modules.top.settings") from error
    if not isinstance(settings, dict):
        raise ValueError("nextpnr modules.top.settings entry is not an object")

    previous = settings.get("timing/allowFail")
    if clear_timing_waiver:
        settings["timing/allowFail"] = FALSE_SETTING
    removed_controls = {
        name: settings.pop(name)
        for name in SERIALIZED_ROUTER_CONTROLS
        if name in settings
    }

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary_name = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=destination.parent,
            prefix=f".{destination.name}.",
            suffix=".tmp",
            delete=False,
        ) as stream:
            temporary_name = stream.name
            json.dump(design, stream, separators=(",", ":"))
            stream.write("\n")
        os.replace(temporary_name, destination)
    except OSError as error:
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except OSError:
                pass
        raise ValueError(f"cannot write {destination}: {error}") from error

    return previous, removed_controls


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="placed nextpnr JSON")
    parser.add_argument("destination", type=Path, help="route-input JSON")
    parser.add_argument(
        "--keep-timing-waiver",
        action="store_true",
        help="retain placement's timing waiver for a diagnostic-only route",
    )
    args = parser.parse_args()

    try:
        previous, removed_controls = prepare_route_input(
            args.source,
            args.destination,
            clear_timing_waiver=not args.keep_timing_waiver,
        )
    except ValueError as error:
        print(f"route-input error: {error}", file=sys.stderr)
        return 2

    if args.keep_timing_waiver:
        print(f"route input timing waiver preserved (was {previous})")
    elif previous is None:
        print("route input had no serialized timing waiver; strict setting added")
    else:
        print(f"route input timing waiver cleared (was {previous})")
    if removed_controls:
        print(
            "route input serialized controls removed: "
            + ", ".join(sorted(removed_controls))
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
