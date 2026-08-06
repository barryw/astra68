#!/usr/bin/env python3
"""Say what an Astra program is, without running it.

Every Astra image carries one 120-byte record in `.astra_program`: its name, a
three-number version, a build id, its author and its copyright. The linker
refuses an image that has none, so this answers about any program the machine
will accept -- and it answers off the ELF, before anything is installed, copied
or launched.

    $ tools/program_info.py sw/userspace/supervisor/build/m68k/astra_supervisor.image.elf
    supervisor 0.1.0
      build      0x00000000
      author     Barry Walker
      copyright  Copyright 2026 Barry Walker

The struct is `AstraProgram` in sw/include/astra/program.h and its layout is a
contract between that header and this file. The record is read out of the
loaded image on purpose: truth about a file is in the file, so this must work
on what actually got installed rather than on an unstripped build sitting in
somebody's tree.

The ELF section walk is `event_catalog.read_section` in this directory, because
the catalog extractor got there first and two copies of it would be two places
for the same off-by-one.
"""

import argparse
import struct
import sys

from event_catalog import CatalogError, read_section

RECORD_SIZE = 120
RECORD_MAGIC = 0x41505247  # "APRG"
RECORD_VERSION = 1
NAME_MAX = 24
AUTHOR_MAX = 32
COPYRIGHT_MAX = 48
SECTION_NAME = ".astra_program"

# magic, record version, major, minor, patch, build id
_HEAD = ">IHHHHI"
_HEAD_SIZE = struct.calcsize(_HEAD)
assert _HEAD_SIZE == 16, _HEAD_SIZE
assert _HEAD_SIZE + NAME_MAX + AUTHOR_MAX + COPYRIGHT_MAX == RECORD_SIZE


class ProgramError(Exception):
    pass


def _text(field):
    """A fixed field is text up to its first NUL. A field with no NUL at all is
    a string that was cut, and this reads what is there rather than inventing a
    terminator -- the macro's static assertions are what stop it happening."""
    return field.split(b"\0")[0].decode("ascii", "replace")


def parse(blob):
    """Turns the section's bytes into what the record says."""
    if len(blob) != RECORD_SIZE:
        # Exactly one record, which is what the linker script asserts. Anything
        # else means this tool and the image disagree about the record's shape,
        # and reading on would produce a plausible wrong answer about who wrote
        # a program and what version it is.
        raise ProgramError(
            "section is %d bytes, not one %d-byte record" %
            (len(blob), RECORD_SIZE))
    magic, version, major, minor, patch, build = struct.unpack_from(
        _HEAD, blob, 0)
    if magic != RECORD_MAGIC:
        raise ProgramError("record has magic 0x%08x, not 0x%08x" %
                           (magic, RECORD_MAGIC))
    if version != RECORD_VERSION:
        # A record from a design that came after this one. Reporting the number
        # is the honest answer; guessing at a layout would attribute a program
        # to whoever happens to be at the right offset.
        raise ProgramError("record version %d, and this tool knows %d" %
                           (version, RECORD_VERSION))
    at = _HEAD_SIZE
    return {
        "name": _text(blob[at:at + NAME_MAX]),
        "version": "%d.%d.%d" % (major, minor, patch),
        "major": major,
        "minor": minor,
        "patch": patch,
        "build_id": build,
        "author": _text(blob[at + NAME_MAX:at + NAME_MAX + AUTHOR_MAX]),
        "copyright": _text(blob[at + NAME_MAX + AUTHOR_MAX:]),
    }


def read(path):
    """The whole operation: an ELF path in, what it says about itself out."""
    _, blob = read_section(path, SECTION_NAME)
    return parse(blob)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", help="an Astra ELF carrying .astra_program")
    arguments = parser.parse_args(argv)

    try:
        program = read(arguments.elf)
    except (CatalogError, ProgramError) as error:
        print("program_info: %s" % error, file=sys.stderr)
        return 1

    print("%s %s" % (program["name"], program["version"]))
    print("  build      0x%08x" % program["build_id"])
    print("  author     %s" % program["author"])
    print("  copyright  %s" % program["copyright"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
