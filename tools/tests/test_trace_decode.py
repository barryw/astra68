"""The trace decoder, against rings built here rather than dumped from QEMU.

A dumped ring tests whatever that boot happened to do. These build the cases
that matter and are hard to provoke on a machine: a wrap that ate an event's
arguments, an id with no catalog, a record that was never committed.
"""

import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import trace_decode  # noqa: E402

CAPACITY = 2047


def ring(records, next_sequence=1, write_index=0, wraps=0, dropped=0):
    blob = bytearray(struct.pack(">IHHIIIII", trace_decode.MAGIC, 1,
                                 trace_decode.RECORD_SIZE, CAPACITY,
                                 next_sequence, write_index, wraps, dropped))
    blob += bytes(4)                       # the header's reserved word
    blob += bytes(CAPACITY * trace_decode.RECORD_SIZE)
    for index, record in records.items():
        at = trace_decode.HEADER_SIZE + index * trace_decode.RECORD_SIZE
        blob[at:at + len(record)] = record
    return bytes(blob)


def kernel_record(sequence, event, arguments=(0, 0, 0, 0)):
    return struct.pack(">IIIHHIIII", sequence, 0, sequence, event, 0,
                       *arguments)


def user_record(sequence, message, flags=2, process=0x10000011, thread=16,
                activity=0, length=0):
    return struct.pack(">IIIHHIIIHH", sequence, 0, sequence,
                       trace_decode.EVENT_USER, flags, process, message,
                       activity, thread, length)


def argument_record(sequence, payload):
    return struct.pack(">IHH", sequence, trace_decode.EVENT_USER_ARGUMENTS,
                       0) + payload.ljust(trace_decode.ARGUMENT_BYTES, b"\0")


CATALOG = {
    0xE0000000: {"file": "src/vfs_host.c", "line": 76, "subsystem":
                 "supervisor", "level": "notice",
                 "format": "namespace bound, %u assigns on session %u",
                 "arguments": ["u32", "u32"]},
    0xE0000080: {"file": "src/thing.c", "line": 9, "subsystem": "vfs",
                 "level": "error", "format": "offset %d",
                 "arguments": ["s32"]},
}
NAMES = {1: "boot", 9: "irq_deliver"}


def test_a_typed_event_is_rendered_from_the_catalog():
    payload = struct.pack(">II", 2, 1)
    blob = ring({0: user_record(1, 0xE0000000, length=8),
                 1: argument_record(2, payload)}, write_index=2)
    _, lines = trace_decode.decode(blob, CATALOG, NAMES)

    assert len(lines) == 1
    assert "namespace bound, 2 assigns on session 1" in lines[0]
    assert "src/vfs_host.c:76" in lines[0]
    assert "notice" in lines[0]
    assert "10000011/16" in lines[0]


def test_a_signed_argument_is_rendered_signed():
    payload = struct.pack(">I", 0xFFFFFFFF)
    blob = ring({0: user_record(1, 0xE0000080, length=4),
                 1: argument_record(2, payload)}, write_index=2)
    _, lines = trace_decode.decode(blob, CATALOG, NAMES)

    assert "offset -1" in lines[0]


def test_kernel_and_user_events_are_one_ordered_stream():
    """Interleaved in the ring and out of index order, they come back in
    sequence order -- which is the property having one ring exists for."""
    blob = ring({0: kernel_record(3, 9),
                 1: user_record(1, 0xE0000000),
                 2: kernel_record(2, 1)}, write_index=3)
    _, lines = trace_decode.decode(blob, CATALOG, NAMES)

    assert [line.split()[1] for line in lines] == ["1", "2", "3"]
    assert "namespace bound" in lines[0]
    assert "boot" in lines[1]
    assert "irq_deliver" in lines[2]


def test_arguments_lost_to_a_wrap_are_said_to_be_lost():
    """The header survives a wrap that ate its arguments, because the writer
    reaches the header first. Rendering whatever now occupies that slot would
    put a plausible wrong number in a log."""
    blob = ring({0: user_record(1, 0xE0000000, length=8),
                 1: kernel_record(900, 1)}, write_index=2)
    _, lines = trace_decode.decode(blob, CATALOG, NAMES)

    user = [line for line in lines if "namespace" in line]
    assert user and "?" in user[0]


def test_an_id_with_no_catalog_is_reported_as_an_id():
    """Guessing at a message would be a decoder inventing a line the machine
    never emitted."""
    blob = ring({0: user_record(1, 0xE0009999)}, write_index=1)
    _, lines = trace_decode.decode(blob, {}, NAMES)

    assert "no catalog" in lines[0]
    assert "0xe0009999" in lines[0].lower()


def test_an_inline_string_needs_no_catalog():
    blob = ring({0: user_record(1, 1, flags=2 | trace_decode.
                                FLAG_INLINE_STRING, length=5),
                 1: argument_record(2, b"hello")}, write_index=2)
    _, lines = trace_decode.decode(blob, {}, NAMES)

    assert "hello" in lines[0]


def test_uncommitted_slots_are_absent_rather_than_zeroes():
    blob = ring({0: kernel_record(1, 1)}, write_index=1)
    _, lines = trace_decode.decode(blob, CATALOG, NAMES)

    assert len(lines) == 1


def test_a_ring_with_the_wrong_magic_is_refused():
    blob = bytearray(ring({}))
    blob[0:4] = b"junk"
    with pytest.raises(trace_decode.TraceError, match="magic"):
        trace_decode.decode(bytes(blob), CATALOG, NAMES)


def test_the_kernel_enum_is_read_from_the_header_not_copied(tmp_path):
    header = tmp_path / "trace.h"
    header.write_text("""
typedef enum KernelTraceEvent {
    KERNEL_TRACE_EVENT_BOOT = 1,
    KERNEL_TRACE_EVENT_PANIC,
    KERNEL_TRACE_EVENT_IRQ_ACK
} KernelTraceEvent;
""")
    names = trace_decode.kernel_event_names(str(header))
    assert names == {1: "boot", 2: "panic", 3: "irq_ack"}


def test_a_header_without_the_enum_is_refused(tmp_path):
    header = tmp_path / "empty.h"
    header.write_text("/* nothing here */\n")
    with pytest.raises(trace_decode.TraceError):
        trace_decode.kernel_event_names(str(header))


def test_catalogs_are_loaded_from_elfs_not_from_a_rendered_file():
    """There is no catalog file. The section in the binary is the catalog, and
    a rendered copy beside it would be a second thing to keep in step."""
    import inspect

    source = inspect.getsource(trace_decode)
    assert "json" not in source
    assert "read_section" in source


def test_the_real_kernel_header_parses():
    """The decoder reads the live enum, so a name added to it is a name this
    tool knows without anyone editing this tool."""
    names = trace_decode.kernel_event_names(
        str(Path(__file__).resolve().parents[2] / "sw/kernel/trace.h"))
    assert names[1] == "boot"
    assert "pmmu_fault" in names.values()
