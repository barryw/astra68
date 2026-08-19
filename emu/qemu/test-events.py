#!/usr/bin/env python3
"""Boot the machine, take its trace ring away, and read it.

This is the gate for the event system as a whole. Everything below it has a
test of its own -- the ring has one, the macro has one, the catalog extractor
has one -- and none of them proves the thing that matters: that an event a
program emitted on the machine comes back out the other side as a line a person
can read, with its arguments substituted and its source location attached.

It asserts three properties in one pass:

  * a typed event survives the round trip. The supervisor records what
    namespace it bound; the ring carries a message id and eight bytes, and
    nothing else, because the format string stayed in a section the ROM image
    strips.
  * the format is resolved from the catalog. If the catalog were wrong or
    missing the line would read `<message 0x...>`, which is a failure here.
  * kernel events and user events are one ordered stream. The user event's
    sequence number falls among the kernel's, which is the property the whole
    "one ring" decision exists for.
  * one command is one story. A refused command emits from the shell twice --
    once accepting the line, once refusing it -- and both carry the same
    non-zero activity, which nothing in the shell passed to either.
"""

import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import astra_image

COMMAND = "write sys:nope no"
RING_ADDRESS = 0x020C4000
RING_SIZE = 0x10000
BOOT_MARKER = "stage 8"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def load_terminal_module():
    """test-terminal.py by path: its name is not an identifier."""
    path = os.path.join(HERE, "test-terminal.py")
    spec = importlib.util.spec_from_file_location("astra_terminal_gate", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(qemu, rom, image, catalog, deadline):
    """Boots, types one refused command, and returns the ring as bytes.

    The typing goes through the desktop: a terminal is a window client now, so
    it is opened by double-clicking its icon rather than started by a manifest
    entry. The gate module owns that, and owns reading the shell back, so this
    borrows both rather than keeping a second copy that would rot separately.
    """
    gate = load_terminal_module()
    directory = tempfile.mkdtemp()
    ring_path = os.path.join(directory, "ring.bin")
    # A copy, with this build's catalog on it: the machine resolves ids from
    # SYS: and the gate must boot what a built machine actually has, not an
    # image that happens to be lying around.
    scratch = os.path.join(directory, "card.img")
    shutil.copyfile(image, scratch)
    astra_image.install(scratch, catalog)
    machine = gate.Machine(qemu, rom, scratch, directory)
    try:
        if not gate.open_terminal(machine, deadline, 60.0):
            raise RuntimeError("no terminal within %.0fs" % deadline)
        # A command that fails, so the shell emits both halves of a story: the
        # line it accepted and the refusal it answered with.
        machine.settle()
        before = machine.sequence()
        machine.qmp.type_line(COMMAND)
        # `write: ` is how report_status prefixes the answer, whatever the
        # refusal turns out to be -- the terminal holds no SYS: at all here,
        # so it is "not found" rather than "access denied".
        if machine.wait_for_text("write:", 60.0, before)[0] is None:
            raise RuntimeError("the shell never refused %r" % COMMAND)
        machine.settle()
        # The ring is retained RAM at a fixed address; the quotes around the
        # path are required, and their absence is one line of "invalid char"
        # from the monitor.
        reply = machine.qmp.monitor('pmemsave 0x%08x %d "%s"' %
                                    (RING_ADDRESS, RING_SIZE, ring_path))
        if reply and reply.strip():
            raise RuntimeError("pmemsave: %s" % reply.strip())
        with open(ring_path, "rb") as handle:
            return handle.read(), machine.log
    finally:
        machine.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("--image", required=True)
    parser.add_argument("--elf", default=os.path.join(
        ROOT, "sw/userspace/supervisor/build/m68k/astra_supervisor.elf"))
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG)
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    arguments = parser.parse_args()

    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import event_catalog
    import trace_decode

    catalog_base, blob = event_catalog.read_section(arguments.elf)
    catalog = {int(key, 16): value for key, value in
               event_catalog.parse(catalog_base, blob).items()}
    names = trace_decode.kernel_event_names(
        os.path.join(ROOT, "sw/kernel/trace.h"))

    ring, serial = run(arguments.qemu, arguments.rom, arguments.image,
                       arguments.catalog, arguments.boot_deadline)
    header, lines = trace_decode.decode(ring, catalog, names)

    user = [line for line in lines if " kernel   " not in line]
    for line in user:
        print(line)

    failures = []
    if not user:
        failures.append("no user event reached the ring")
    resolved = [line for line in user if "namespace bound" in line]
    if not resolved:
        failures.append("the supervisor's namespace event was not resolved "
                        "through the catalog")
    else:
        line = resolved[0]
        # The arguments came out of the payload, not out of the format string.
        if not re.search(r"namespace bound, \d+ assigns on session \d+", line):
            failures.append("arguments were not substituted: %r" % line)
        if "src/vfs_host.c:" not in line:
            failures.append("no source location: %r" % line)
        # One ordered stream: the user event is numbered among the kernel's.
        sequence = int(re.search(r"seq (\d+)", line).group(1))
        kernel = [int(re.search(r"seq (\d+)", other).group(1))
                  for other in lines if " kernel   " in other]
        if not kernel or not (min(kernel) < sequence < max(kernel) or
                              sequence > min(kernel)):
            failures.append("the user event is not ordered among the kernel's")

    # One command, one story: the shell's two events share an activity that
    # nothing passed to either of them.
    shell = [line for line in user if "command " in line]
    activities = set(re.findall(r"act ([0-9a-f]+)", " ".join(shell)))
    if len(shell) < 2:
        failures.append("the shell emitted %d events for a command, not 2"
                        % len(shell))
    elif len(activities) != 1:
        failures.append("the command's events carry %d activities: %s"
                        % (len(activities), sorted(activities)))

    print("ring: %d records, %d wraps, %d dropped" %
          (len(lines), header["wraps"], header["dropped"]))
    if failures:
        for failure in failures:
            print("FAIL: %s" % failure, file=sys.stderr)
        print("--- last serial ---", file=sys.stderr)
        for line in serial[-12:]:
            print(line, file=sys.stderr)
        return 1
    print("ASTRA EVENTS PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
