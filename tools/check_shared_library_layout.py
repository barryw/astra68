#!/usr/bin/env python3
"""Reject Astra shared libraries with unusable PLT or unwind layout."""

import argparse
import sys

from event_catalog import CatalogError, read_section
from check_dynamic_relocations import DynamicRelocationError
from check_dynamic_relocations import violations as relocation_violations


def check(path, require_frame_registration=True):
    """Require frame registration data and a real PLT when one is used."""
    try:
        problems = relocation_violations(path)
    except DynamicRelocationError as error:
        raise CatalogError(str(error)) from error
    if problems:
        raise CatalogError("; ".join(problems))
    for section in ((".eh_frame", ".init_array", ".fini_array")
                    if require_frame_registration else ()):
        try:
            _, contents = read_section(path, section)
        except CatalogError as error:
            raise CatalogError(
                "%s has no %s; shared-library exceptions cannot register "
                "their frame data" % (path, section)) from error
        if not contents:
            raise CatalogError("%s has an empty %s" % (path, section))
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
    parser.add_argument("--no-frame-registration", action="store_true",
                        help="validate the self-relocating process interpreter")
    parser.add_argument("elf")
    arguments = parser.parse_args(argv)
    try:
        check(arguments.elf, not arguments.no_frame_registration)
    except CatalogError as error:
        print("check_shared_library_layout: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
