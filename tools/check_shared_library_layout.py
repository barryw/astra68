#!/usr/bin/env python3
"""Reject Astra shared libraries whose linker-generated PLT was displaced."""

import argparse
import sys

from event_catalog import CatalogError, read_section
from check_dynamic_relocations import DynamicRelocationError
from check_dynamic_relocations import violations as relocation_violations


def check(path):
    """Require a real PLT section whenever the ELF carries PLT relocations."""
    try:
        problems = relocation_violations(path)
    except DynamicRelocationError as error:
        raise CatalogError(str(error)) from error
    if problems:
        raise CatalogError("; ".join(problems))
    try:
        _, relocations = read_section(path, ".rela.plt")
    except CatalogError:
        return
    if not relocations:
        return
    try:
        _, plt = read_section(path, ".plt")
    except CatalogError as error:
        raise CatalogError(
            "%s has PLT relocations but no .plt output section; "
            "linker-generated PC-relative thunks were folded into another "
            "section and may target unmapped memory" % path) from error
    if not plt:
        raise CatalogError("%s has PLT relocations but an empty .plt" % path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    arguments = parser.parse_args(argv)
    try:
        check(arguments.elf)
    except CatalogError as error:
        print("check_shared_library_layout: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
