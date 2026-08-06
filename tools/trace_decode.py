#!/usr/bin/env python3
"""Render Astra's trace ring as something a person can read.

The ring is one ordered stream carrying two kinds of record: the kernel's own
events, and the user events a program emits. Both are 32 bytes, both carry a
commit sequence, and ordering is by that sequence and never by a timestamp --
which is what makes the order correct regardless of what any clock does.

A user event's message id is the address of a descriptor in a section the ROM
image strips, so the format string, the file and the line are resolved here,
from a catalog produced by tools/event_catalog.py. That is the whole reason an
occurrence costs 32 bytes: everything static about a message stayed behind.

The ring lives at a fixed address in retained RAM, so getting one off a machine
is a memory dump:

    (qemu) pmemsave 0x020c4000 65536 "/tmp/ring.bin"
    tools/trace_decode.py /tmp/ring.bin --elf astra_supervisor.elf

The catalog is read straight out of the ELF's .astra_events section. There is
no catalog file and no serialization step: the section is the catalog, a
rendered copy of it would be a second thing to keep in step with the binary,
and the only reader was this script.
"""

import argparse
import re
import struct
import sys

sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parent))
from pathlib import Path

RING_ADDRESS = 0x020C4000
RING_SIZE = 0x10000
HEADER_SIZE = 32
RECORD_SIZE = 32
MAGIC = 0x41545243  # "ATRC"

EVENT_USER = 0xE000
EVENT_USER_ARGUMENTS = 0xE001

LEVELS = ["debug", "info", "notice", "warning", "error"]
FLAG_PRESENTED = 0x0008
FLAG_INLINE_STRING = 0x0010
FLAG_CONTINUED = 0x0020

ARGUMENT_BYTES = 24


class TraceError(Exception):
    pass


def kernel_event_names(header_path):
    """The KernelTraceEvent enum, read from the header rather than copied.

    A second copy of this list would be a list that drifts, and a decoder that
    silently renamed an event would be worse than one that printed a number.
    """
    text = Path(header_path).read_text()
    match = re.search(r"typedef enum KernelTraceEvent \{(.*?)\}", text,
                      re.DOTALL)
    if not match:
        raise TraceError("no KernelTraceEvent enum in %s" % header_path)
    names = {}
    value = 0
    for line in match.group(1).splitlines():
        entry = re.match(r"\s*(KERNEL_TRACE_EVENT_\w+)\s*(?:=\s*(\d+))?\s*,?",
                         line)
        if not entry:
            continue
        if entry.group(2) is not None:
            value = int(entry.group(2))
        names[value] = entry.group(1)[len("KERNEL_TRACE_EVENT_"):].lower()
        value += 1
    if not names:
        raise TraceError("no enumerators in %s" % header_path)
    return names


def load_catalogs(paths):
    """Message id -> descriptor, from the ELFs the events were emitted by."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import event_catalog

    catalog = {}
    for path in paths or []:
        base, blob = event_catalog.read_section(path)
        for message_id, entry in event_catalog.parse(base, blob).items():
            catalog[int(message_id, 16)] = entry
    return catalog


def read_header(blob):
    if len(blob) < HEADER_SIZE:
        raise TraceError("ring image is %d bytes" % len(blob))
    magic, abi, record_size, capacity, next_sequence, write_index, wraps, \
        dropped = struct.unpack_from(">IHHIIIII", blob, 0)
    if magic != MAGIC:
        raise TraceError("ring magic is 0x%08x, not 0x%08x" % (magic, MAGIC))
    if record_size != RECORD_SIZE:
        raise TraceError("ring records are %d bytes" % record_size)
    return {"abi": abi, "capacity": capacity, "next_sequence": next_sequence,
            "write_index": write_index, "wraps": wraps, "dropped": dropped}


def _slot(blob, index):
    at = HEADER_SIZE + index * RECORD_SIZE
    return blob[at:at + RECORD_SIZE]


def _argument_slot_of(blob, header, index, commit, raw):
    """The index holding this record's arguments, or None.

    Positional, not by reading a field: an argument slot and a kernel record
    put different things at the same offsets, so telling them apart by content
    means guessing. The arguments are in the following slot and carry the
    following sequence number, and anything else means the ring wrapped over
    them.
    """
    event, = struct.unpack_from(">H", raw, 12)
    if event != EVENT_USER:
        return None
    length, = struct.unpack_from(">H", raw, 30)
    if length == 0:
        return None
    following = (index + 1) % header["capacity"]
    argument_commit, argument_event = struct.unpack_from(
        ">IH", _slot(blob, following), 0)
    expected = commit + 1 if commit + 1 != 0 else 1
    if argument_commit == expected and argument_event == EVENT_USER_ARGUMENTS:
        return following
    return None


def records(blob, header):
    """Every committed slot, in sequence order. Uncommitted slots are absent
    rather than rendered as zeroes, and an argument slot belongs to the record
    before it rather than standing on its own."""
    committed = []
    arguments = set()
    for index in range(header["capacity"]):
        raw = _slot(blob, index)
        commit, = struct.unpack_from(">I", raw, 0)
        if commit == 0:
            continue
        committed.append((commit, index, raw))
        following = _argument_slot_of(blob, header, index, commit, raw)
        if following is not None:
            arguments.add(following)
    found = [item for item in committed if item[1] not in arguments]
    found.sort(key=lambda item: item[0])
    return found


def decode_user(blob, header, index, raw):
    (commit, high, low, event, flags, process, message, activity, thread,
     length) = struct.unpack_from(">IIIHHIIIHH", raw, 0)
    payload = b""
    if length:
        following = _argument_slot_of(blob, header, index, commit, raw)
        # The ring wrapped over them. Saying so beats rendering whatever now
        # occupies that slot as though it were the argument.
        payload = None if following is None else \
            _slot(blob, following)[8:8 + length]
    return {"sequence": commit, "ticks": (high << 32) | low, "kind": "user",
            "process": process, "thread": thread, "activity": activity,
            "message": message, "flags": flags, "payload": payload}


def decode_kernel(raw):
    commit, high, low, event, flags, a0, a1, a2, a3 = struct.unpack_from(
        ">IIIHHIIII", raw, 0)
    return {"sequence": commit, "ticks": (high << 32) | low, "kind": "kernel",
            "event": event, "flags": flags, "arguments": [a0, a1, a2, a3]}


def render_arguments(entry, payload):
    """Substitutes the values into the message's format. The format never
    travelled; it came from the catalog, which is why an occurrence is small."""
    values = []
    for index, kind in enumerate(entry.get("arguments", [])):
        at = index * 4
        if payload is None or at + 4 > len(payload):
            values.append("?")
            continue
        value, = struct.unpack_from(">I", payload, at)
        if kind == "s32" and value >= 0x80000000:
            value -= 0x100000000
        values.append(str(value))
    text = entry["format"]
    for value in values:
        text = re.sub(r"%[0-9]*[udxs]", value, text, count=1)
    return text


def render(record, catalog, kernel_names):
    where = "seq %-6d" % record["sequence"]
    if record["kind"] == "kernel":
        name = kernel_names.get(record["event"], "event %d" % record["event"])
        arguments = " ".join("0x%08x" % value
                             for value in record["arguments"])
        return "%s kernel   %-20s %s" % (where, name, arguments)

    level = LEVELS[record["flags"] & 0x7]
    who = "%08x/%d" % (record["process"], record["thread"])
    if record["activity"]:
        who += " act %08x" % record["activity"]

    if record["flags"] & FLAG_INLINE_STRING:
        if record["payload"] is None:
            body = "<arguments lost to a wrap>"
        else:
            body = record["payload"].decode("ascii", "replace")
            if record["flags"] & FLAG_CONTINUED:
                body += "\\"
        return "%s %-8s %s %s" % (where, level, who, body)

    entry = catalog.get(record["message"])
    if entry is None:
        # An id with no catalog is reported as an id. Guessing at a message
        # would be a decoder inventing a line the machine never emitted.
        return "%s %-8s %s <message 0x%08x, no catalog>" % (
            where, level, who, record["message"])
    body = render_arguments(entry, record["payload"])
    return "%s %-8s %s %s   (%s:%d)" % (where, level, who, body,
                                        entry["file"], entry["line"])


def decode(blob, catalog, kernel_names):
    header = read_header(blob)
    lines = []
    for commit, index, raw in records(blob, header):
        event, = struct.unpack_from(">H", raw, 12)
        record = decode_user(blob, header, index, raw) if event == EVENT_USER \
            else decode_kernel(raw)
        lines.append(render(record, catalog, kernel_names))
    return header, lines


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ring", help="a dump of the 64 KiB trace ring")
    parser.add_argument("--elf", action="append",
                        help="an ELF whose events may appear here; repeatable")
    parser.add_argument("--trace-header",
                        default=str(Path(__file__).resolve().parents[1] /
                                    "sw/kernel/trace.h"),
                        help="where the KernelTraceEvent enum is read from")
    parser.add_argument("--user-only", action="store_true",
                        help="hide the kernel's own events")
    arguments = parser.parse_args(argv)

    try:
        blob = Path(arguments.ring).read_bytes()
        catalog = load_catalogs(arguments.elf)
        names = kernel_event_names(arguments.trace_header)
        header, lines = decode(blob, catalog, names)
    except (TraceError, OSError, ValueError) as error:
        print("trace_decode: %s" % error, file=sys.stderr)
        return 1

    print("ring: %d records, %d wraps, %d dropped" %
          (len(lines), header["wraps"], header["dropped"]))
    for line in lines:
        if arguments.user_only and " kernel   " in line:
            continue
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
