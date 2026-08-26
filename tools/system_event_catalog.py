#!/usr/bin/env python3
"""Assign dense system event IDs and merge their catalog fragments."""

import argparse
import sys
from pathlib import Path

import event_catalog


CATALOG_BASE = 0xE0000000


class SystemCatalogError(Exception):
    pass


def _validate(blob, base):
    try:
        if not blob or len(blob) % event_catalog.DESCRIPTOR_SIZE:
            raise SystemCatalogError("catalog fragment is not whole descriptors")
        event_catalog.parse(base, blob)
    except event_catalog.CatalogError as error:
        raise SystemCatalogError(str(error)) from error


def next_base(fragments):
    base = CATALOG_BASE
    for blob in fragments:
        _validate(blob, base)
        base += len(blob)
    return base


def linker_script(fragments):
    return "ASTRA_EVENT_IMAGE_CATALOG_BASE = 0x%08x;\n" % next_base(fragments)


def merge_elf_catalogs(paths):
    expected = CATALOG_BASE
    merged = bytearray()
    for path in paths:
        try:
            base, blob = event_catalog.read_section(str(path))
        except event_catalog.CatalogError as error:
            raise SystemCatalogError(str(error)) from error
        if base != expected:
            raise SystemCatalogError(
                "%s is based at 0x%08x; expected contiguous base 0x%08x" %
                (path, base, expected))
        _validate(blob, base)
        merged += blob
        expected += len(blob)
    if not merged:
        raise SystemCatalogError("no system catalog fragments")
    return CATALOG_BASE, bytes(merged)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    base_parser = subcommands.add_parser("base")
    base_parser.add_argument("catalog", nargs="+")
    merge_parser = subcommands.add_parser("merge")
    merge_parser.add_argument("output")
    merge_parser.add_argument("elf", nargs="+")
    arguments = parser.parse_args(argv)

    try:
        if arguments.command == "base":
            fragments = [Path(path).read_bytes() for path in arguments.catalog]
            sys.stdout.write(linker_script(fragments))
        else:
            _, blob = merge_elf_catalogs(arguments.elf)
            Path(arguments.output).write_bytes(blob)
    except (OSError, SystemCatalogError) as error:
        print("system_event_catalog: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
