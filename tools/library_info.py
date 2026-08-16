#!/usr/bin/env python3
"""Print the identity embedded in an Astra shared library."""

import argparse
import struct
import sys

from event_catalog import CatalogError, read_section

RECORD_SIZE = 128
RECORD_MAGIC = 0x414C4942
RECORD_VERSION = 1
TARGET_M68030 = 0x4D303330
NAME_MAX = 24
AUTHOR_MAX = 32
COPYRIGHT_MAX = 40
SECTION_NAME = ".astra_library"
_HEAD = ">IHHHHHHHHIII"
_HEAD_SIZE = struct.calcsize(_HEAD)
assert _HEAD_SIZE == 32
assert _HEAD_SIZE + NAME_MAX + AUTHOR_MAX + COPYRIGHT_MAX == RECORD_SIZE


class LibraryError(Exception):
    pass


def _text(field):
    return field.split(b"\0")[0].decode("ascii", "replace")


def parse(blob):
    if len(blob) != RECORD_SIZE:
        raise LibraryError("section is %d bytes, not one %d-byte record" %
                           (len(blob), RECORD_SIZE))
    fields = struct.unpack_from(_HEAD, blob)
    (magic, record_version, header_size, major, minor, patch, abi_major,
     abi_minor, flags, target, build_id, exports_offset) = fields
    if magic != RECORD_MAGIC:
        raise LibraryError("record has bad magic 0x%08x" % magic)
    if record_version != RECORD_VERSION:
        raise LibraryError("record version %d, and this tool knows %d" %
                           (record_version, RECORD_VERSION))
    if header_size != RECORD_SIZE:
        raise LibraryError("record declares size %d, expected %d" %
                           (header_size, RECORD_SIZE))
    if target != TARGET_M68030:
        raise LibraryError("unsupported target 0x%08x" % target)
    at = _HEAD_SIZE
    return {
        "name": _text(blob[at:at + NAME_MAX]),
        "version": "%d.%d.%d" % (major, minor, patch),
        "major": major,
        "minor": minor,
        "patch": patch,
        "abi_major": abi_major,
        "abi_minor": abi_minor,
        "flags": flags,
        "target": target,
        "build_id": build_id,
        "exports_offset": exports_offset,
        "author": _text(blob[at + NAME_MAX:at + NAME_MAX + AUTHOR_MAX]),
        "copyright": _text(blob[at + NAME_MAX + AUTHOR_MAX:]),
    }


def read(path):
    _, blob = read_section(path, SECTION_NAME)
    return parse(blob)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    arguments = parser.parse_args(argv)
    try:
        library = read(arguments.elf)
    except (CatalogError, LibraryError) as error:
        print("library_info: %s" % error, file=sys.stderr)
        return 1
    print("%s %s ABI %d.%d" %
          (library["name"], library["version"], library["abi_major"],
           library["abi_minor"]))
    print("  build      0x%08x" % library["build_id"])
    print("  author     %s" % library["author"])
    print("  copyright  %s" % library["copyright"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
