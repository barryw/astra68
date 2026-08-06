#!/usr/bin/env python3
"""Show the event catalog carried by an Astra ELF.

Every ASTRA_EVENT call site emits a 128-byte descriptor into `.astra_events`,
a section the loader never maps and the ROM image strips. This reads them back
out: message id -> subsystem, level, file, line, format, argument types.

The message id is the descriptor's address, which is the section's base plus
the record's offset. Nothing hashes anything and nothing is registered at
startup, so an id is stable for the life of a build and a reader resolves it
against the catalog for the image the emitting process was running.

The struct is `AstraEventDescriptor` in sw/include/astra/event_descriptor.h.
Its layout is a contract between that header and this file; the runtime's host
test asserts the offsets below so the two cannot drift apart silently.

This writes nothing. It used to render JSON, which had exactly one reader --
trace_decode.py, in this directory -- and a serialization between two halves of
one program is ceremony, not an interface. Both read the section through
`read_section` and `parse` instead, and the printed form here is for a person.

When a catalog does have to be a file -- a bundle carrying its own, or the
events service resolving ids on the machine -- the file is the section's bytes
verbatim, which `objcopy -O binary --only-section=.astra_events` already
produces. A fixed 128-byte record is an index on a 30 MHz machine; a parse is
not.
"""

import argparse
import struct
import sys

DESCRIPTOR_SIZE = 128
DESCRIPTOR_MAGIC = 0x41455644  # "AEVD"
FILE_MAX = 48
FORMAT_MAX = 64
SECTION_NAME = ".astra_events"

# magic, line, subsystem, level, argument_count, 4 types, 3 reserved
_HEAD = ">IHBBB4s3x"
_HEAD_SIZE = struct.calcsize(_HEAD)
assert _HEAD_SIZE == 16, _HEAD_SIZE
assert _HEAD_SIZE + FILE_MAX + FORMAT_MAX == DESCRIPTOR_SIZE

LEVELS = ["debug", "info", "notice", "warning", "error"]
SUBSYSTEMS = ["kernel", "runtime", "supervisor", "storage", "vfs", "shell",
              "input", "display"]
ARGUMENT_TYPES = ["none", "u32", "s32", "status", "handle", "u64", "string"]


class CatalogError(Exception):
    pass


def _name(table, index, what):
    """A number this build does not know is reported, never rendered as a
    guess: a catalog that invented a subsystem name would be worse than one
    that admitted the number came from a newer build."""
    if index < len(table):
        return table[index]
    raise CatalogError("unknown %s %d" % (what, index))


def read_section(path):
    """Returns (base_address, bytes) for .astra_events, big-endian ELF32."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"\x7fELF":
        raise CatalogError("%s is not an ELF file" % path)
    if data[4] != 1 or data[5] != 2:
        raise CatalogError("%s is not a big-endian 32-bit ELF" % path)
    e_shoff, = struct.unpack_from(">I", data, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    if e_shoff == 0 or e_shnum == 0:
        raise CatalogError("%s has no section headers" % path)

    def header(index):
        at = e_shoff + index * e_shentsize
        # name, type, flags, addr, offset, size
        return struct.unpack_from(">IIIIII", data, at)

    _, _, _, _, str_offset, str_size = header(e_shstrndx)
    names = data[str_offset:str_offset + str_size]
    for index in range(e_shnum):
        name_offset, _, _, address, offset, size = header(index)
        end = names.index(b"\0", name_offset)
        if names[name_offset:end].decode("ascii") != SECTION_NAME:
            continue
        return address, data[offset:offset + size]
    raise CatalogError("%s has no %s section" % (path, SECTION_NAME))


def parse(base, blob):
    if len(blob) % DESCRIPTOR_SIZE != 0:
        # The descriptor changed shape and this file did not. Reading on would
        # produce a catalog that looked plausible and was wrong throughout.
        raise CatalogError(
            "section is %d bytes, not a multiple of %d" %
            (len(blob), DESCRIPTOR_SIZE))
    catalog = {}
    for offset in range(0, len(blob), DESCRIPTOR_SIZE):
        record = blob[offset:offset + DESCRIPTOR_SIZE]
        magic, line, subsystem, level, count, types = struct.unpack_from(
            _HEAD, record, 0)
        if magic != DESCRIPTOR_MAGIC:
            raise CatalogError(
                "descriptor at +0x%x has magic 0x%08x, not 0x%08x" %
                (offset, magic, DESCRIPTOR_MAGIC))
        if count > 4:
            raise CatalogError("descriptor at +0x%x declares %d arguments" %
                               (offset, count))
        source = record[_HEAD_SIZE:_HEAD_SIZE + FILE_MAX]
        fmt = record[_HEAD_SIZE + FILE_MAX:]
        catalog["0x%08x" % (base + offset)] = {
            "subsystem": _name(SUBSYSTEMS, subsystem, "subsystem"),
            "level": _name(LEVELS, level, "level"),
            "file": source.split(b"\0")[0].decode("ascii", "replace"),
            "line": line,
            "format": fmt.split(b"\0")[0].decode("ascii", "replace"),
            "arguments": [_name(ARGUMENT_TYPES, types[index], "argument type")
                          for index in range(count)],
        }
    return catalog


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", help="an Astra ELF carrying .astra_events")
    arguments = parser.parse_args(argv)

    try:
        base, blob = read_section(arguments.elf)
        catalog = parse(base, blob)
    except CatalogError as error:
        print("event_catalog: %s" % error, file=sys.stderr)
        return 1

    for message_id in sorted(catalog):
        entry = catalog[message_id]
        print("%s  %-8s %-11s %-46s %s:%d" % (
            message_id, entry["level"], entry["subsystem"], entry["format"],
            entry["file"], entry["line"]))
    print("%d messages" % len(catalog), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
